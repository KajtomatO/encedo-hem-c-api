# encedo-hem-c-api — the Encedo HEM C SDK.
# implements: REQ-BUILD-001
#
# Cross-platform build (Linux GCC/Clang, Windows MinGW/MSYS2) producing static
# and shared `encedo-hem` libraries plus, later, the hem-tool executable.
# See ARCHITECTURE.md §1 (fixed decisions) and §10 (directory layout).

cmake_minimum_required(VERSION 3.20)

project(encedo-hem
  VERSION 1.1.0
  DESCRIPTION "Encedo HEM C SDK"
  LANGUAGES C)

# --- global build settings ------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)                 # strict C99, no GNU extensions

# Hidden by default everywhere; only EHEM_API symbols escape (REQ-API-006).
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Warnings-as-errors baseline (REQ-BUILD-001).
if(MSVC)
  set(EHEM_WARNING_FLAGS /W4 /WX)
else()
  set(EHEM_WARNING_FLAGS -Wall -Wextra -Werror)
endif()

# Optional sanitizers for the unit suite on Linux/Clang/GCC (STEP-M1-040 DoD).
option(EHEM_SANITIZE "Build with ASan/UBSan (Debug, non-MSVC)" OFF)
if(EHEM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- build configuration switches (REQ-BUILD-006) --------------------------
# implements: REQ-BUILD-006 — which SDK variants are built, how hem-tool links
# the SDK, how the third-party libraries link. Every default is the pre-M12
# behaviour. Changing EHEM_DEPS_LINK in an existing build directory keeps
# CMake's cached library lookups — use a fresh one (./dev keeps one per set).
option(EHEM_BUILD_SHARED "Build and install the shared SDK (libencedo-hem.so/.dll)" ON)
option(EHEM_BUILD_STATIC "Build and install the static SDK (libencedo-hem.a)" ON)
set(EHEM_TOOL_LINK "static" CACHE STRING
  "How hem-tool links the SDK: static (it carries the SDK) | shared")
set_property(CACHE EHEM_TOOL_LINK PROPERTY STRINGS static shared)
set(EHEM_DEPS_LINK "default" CACHE STRING
  "How libcurl and the crypto library link: default (as found) | static | shared")
set_property(CACHE EHEM_DEPS_LINK PROPERTY STRINGS default static shared)
# hem-tool can be turned off for a library-only consumer.
option(EHEM_BUILD_TOOL "Build the hem-tool CLI" ON)
# implements: REQ-BUILD-007 — the crypto library behind src/crypto_shim.h:
# wolfSSL (the default) or OpenSSL 3; exactly one shim source per configure.
set(EHEM_CRYPTO_BACKEND "wolfssl" CACHE STRING
  "The crypto library behind the crypto shim: wolfssl | openssl (3.x)")
set_property(CACHE EHEM_CRYPTO_BACKEND PROPERTY STRINGS wolfssl openssl)

if(NOT EHEM_CRYPTO_BACKEND MATCHES "^(wolfssl|openssl)$")
  message(FATAL_ERROR "EHEM_CRYPTO_BACKEND must be wolfssl or openssl, not '${EHEM_CRYPTO_BACKEND}'")
endif()
if(NOT EHEM_TOOL_LINK MATCHES "^(static|shared)$")
  message(FATAL_ERROR "EHEM_TOOL_LINK must be static or shared, not '${EHEM_TOOL_LINK}'")
endif()
if(NOT EHEM_DEPS_LINK MATCHES "^(default|static|shared)$")
  message(FATAL_ERROR "EHEM_DEPS_LINK must be default, static or shared, not '${EHEM_DEPS_LINK}'")
endif()
if(NOT EHEM_BUILD_SHARED AND NOT EHEM_BUILD_STATIC)
  message(FATAL_ERROR "EHEM_BUILD_SHARED and EHEM_BUILD_STATIC are both OFF - "
                      "nothing would be built; turn at least one on")
endif()
if(EHEM_BUILD_TOOL AND EHEM_TOOL_LINK STREQUAL "shared" AND NOT EHEM_BUILD_SHARED)
  message(FATAL_ERROR "EHEM_TOOL_LINK=shared needs the shared SDK, but "
                      "EHEM_BUILD_SHARED is OFF")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)               # defines BUILD_TESTING (default ON), enables CTest

# Third-party libraries the link closure may name that are never archived here:
# glibc stays dynamic (a static glibc breaks NSS/getaddrinfo), and Windows'
# system DLLs are reached through MinGW import libraries.
set(EHEM_SYSTEM_LIBS m pthread dl rt c resolv
    ws2_32 crypt32 bcrypt advapi32 secur32 iphlpapi wldap32 normaliz user32
    kernel32 gdi32 ole32 shell32 rpcrt4 userenv)

# EHEM_DEPS_LINK=static: the archives of a static link closure — each library
# name as `pkg-config --static` prints it, resolved to lib<name>.a. The
# package's own library (`primary`) must come from the package's own libdir —
# never another copy found elsewhere (a system archive of another version
# under the prefix's headers is exactly the mix REQ-API-010 refuses); the
# others from the closure's -L dirs, then the default search path. A missing
# archive is a configure error naming `what` and the library — never a silent
# fall-back to a shared library.
function(ehem_static_closure out what primary primary_dir dirs)
  set(_paths "")
  foreach(_name IN LISTS ARGN)
    if(_name IN_LIST EHEM_SYSTEM_LIBS)
      list(APPEND _paths "${_name}")
      continue()
    endif()
    string(MAKE_C_IDENTIFIER "EHEM_ARCHIVE_${_name}" _var)
    if(_name STREQUAL primary AND primary_dir)
      find_library(${_var}
        NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}${_name}${CMAKE_STATIC_LIBRARY_SUFFIX}
        PATHS ${primary_dir} NO_DEFAULT_PATH)
    else()
      find_library(${_var}
        NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}${_name}${CMAKE_STATIC_LIBRARY_SUFFIX}
        HINTS ${dirs})
    endif()
    if(NOT ${_var})
      message(FATAL_ERROR "EHEM_DEPS_LINK=static: ${what} needs lib${_name}, and "
        "there is no static archive of it (lib${_name}${CMAKE_STATIC_LIBRARY_SUFFIX}) "
        "- point CMAKE_PREFIX_PATH / PKG_CONFIG_PATH at a prefix that has one, "
        "or use EHEM_DEPS_LINK=default")
    endif()
    list(APPEND _paths "${${_var}}")
    unset(${_var} CACHE)
  endforeach()
  list(REMOVE_DUPLICATES _paths)
  set(${out} "${_paths}" PARENT_SCOPE)
endfunction()

# The link preference steers every library lookup of the two dependencies;
# restored afterwards so the test framework is found as usual.
set(_ehem_saved_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
if(EHEM_DEPS_LINK STREQUAL "static")
  set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(CURL_USE_STATIC_LIBS ON)           # FindCURL module mode (CMake >= 3.28)
elseif(EHEM_DEPS_LINK STREQUAL "shared")
  if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_IMPORT_LIBRARY_SUFFIX}")
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
endif()

# HTTPS transport dependency (REQ-NET-002). The FindCURL module provides the
# imported target CURL::libcurl; no cURL type appears in public headers.
find_package(CURL REQUIRED)
# The version found (FindCURL: CURL_VERSION_STRING; curl's own package config
# and newer CMake: CURL_VERSION) — the unit test's oracle for REQ-API-011.
if(CURL_VERSION_STRING)
  set(EHEM_CURL_VERSION "${CURL_VERSION_STRING}")
else()
  set(EHEM_CURL_VERSION "${CURL_VERSION}")
endif()

# Crypto primitives for the auth flow (REQ-AUTH-001) from the backend
# EHEM_CRYPTO_BACKEND names (REQ-BUILD-007). Like libcurl, no type of either
# library appears in a public header (the crypto shim contains it), so it links
# privately into the shared library. pkg-config also reads libcurl's static
# closure below, whichever the backend.
find_package(PkgConfig REQUIRED)
if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
  # wolfCrypt via wolfSSL, used from M2 on. The dev distros (Debian/Ubuntu apt,
  # MSYS2 pacman) ship a pkg-config file but no CMake package config, so
  # discover via pkg-config into the imported target PkgConfig::WOLFSSL.
  pkg_check_modules(WOLFSSL REQUIRED IMPORTED_TARGET wolfssl)
  set(EHEM_CRYPTO_NAME "wolfSSL")
  set(EHEM_CRYPTO_VERSION "${WOLFSSL_VERSION}")
  set(EHEM_CRYPTO_LINK_LIBS PkgConfig::WOLFSSL)
else()
  # OpenSSL 3: libcrypto only (libssl is libcurl's business) — FindOpenSSL's
  # imported target OpenSSL::Crypto; static archives on request.
  if(EHEM_DEPS_LINK STREQUAL "static")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
  endif()
  find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
  set(EHEM_CRYPTO_NAME "OpenSSL")
  set(EHEM_CRYPTO_VERSION "${OPENSSL_VERSION}")
  set(EHEM_CRYPTO_LINK_LIBS OpenSSL::Crypto)
endif()

if(EHEM_DEPS_LINK STREQUAL "static")
  if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
    # wolfSSL: the `pkg-config --static` closure, every member an archive.
    # (OpenSSL: FindOpenSSL's static mode adds its own system libraries.)
    ehem_static_closure(_ehem_wolfssl_archives "wolfSSL (pkg-config wolfssl)"
      wolfssl "${WOLFSSL_LIBDIR}" "${WOLFSSL_STATIC_LIBRARY_DIRS}"
      ${WOLFSSL_STATIC_LIBRARIES})
    set_property(TARGET PkgConfig::WOLFSSL
      PROPERTY INTERFACE_LINK_LIBRARIES "${_ehem_wolfssl_archives}")
  endif()
  # libcurl: curl's own package config (a prefix built by
  # scripts/release/build-deps.sh) carries its closure on its targets; FindCURL
  # module mode finds the archive but not its dependencies — add the
  # `pkg-config --static libcurl` closure, every member an archive.
  get_target_property(_ehem_curl_aliased CURL::libcurl ALIASED_TARGET)
  if(NOT CURL_DIR AND NOT _ehem_curl_aliased)
    pkg_check_modules(EHEM_PC_CURL QUIET libcurl)
    if(NOT EHEM_PC_CURL_FOUND)
      message(FATAL_ERROR "EHEM_DEPS_LINK=static: no libcurl.pc to read libcurl's "
                          "static dependencies from")
    endif()
    ehem_static_closure(_ehem_curl_archives "libcurl (pkg-config libcurl)"
      curl "${EHEM_PC_CURL_LIBDIR}" "${EHEM_PC_CURL_STATIC_LIBRARY_DIRS}"
      ${EHEM_PC_CURL_STATIC_LIBRARIES})
    list(GET _ehem_curl_archives 0 _ehem_curl_archive)
    list(REMOVE_AT _ehem_curl_archives 0)
    set_target_properties(CURL::libcurl PROPERTIES
      IMPORTED_LOCATION "${_ehem_curl_archive}"
      IMPORTED_LOCATION_RELEASE "${_ehem_curl_archive}"
      INTERFACE_LINK_LIBRARIES "${_ehem_curl_archives}")
    set_property(TARGET CURL::libcurl APPEND PROPERTY
      INTERFACE_COMPILE_DEFINITIONS CURL_STATICLIB)
  endif()
endif()
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_ehem_saved_suffixes}")

# How the resolved dependencies link — a build fact ehem_build_info_get()
# reports (REQ-API-011) and `hem-tool --version` prints (REQ-TOOL-025): a static
# archive (.a / .lib — but not a MinGW .dll.a import library) or a shared
# library. `lib` is a library path or an imported target.
function(ehem_link_mode lib out)
  set(_mode dynamic)
  if(TARGET "${lib}")
    get_target_property(_aliased "${lib}" ALIASED_TARGET)
    if(_aliased)
      set(lib "${_aliased}")
    endif()
    get_target_property(_type "${lib}" TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
      set(_mode static)
    elseif(_type STREQUAL "UNKNOWN_LIBRARY")
      foreach(_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE
                    IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_DEBUG)
        get_target_property(_loc "${lib}" ${_prop})
        if(_loc)
          ehem_link_mode("${_loc}" _mode)
          break()
        endif()
      endforeach()
    endif()
  elseif(lib MATCHES "\\.(a|lib)$" AND NOT lib MATCHES "\\.dll\\.a$")
    set(_mode static)
  endif()
  set(${out} ${_mode} PARENT_SCOPE)
endfunction()

set(EHEM_CRYPTO_LINK dynamic)
if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
  foreach(_lib IN LISTS WOLFSSL_LINK_LIBRARIES)
    if(_lib MATCHES "wolfssl")
      ehem_link_mode("${_lib}" EHEM_CRYPTO_LINK)
      break()
    endif()
  endforeach()
else()
  ehem_link_mode(OpenSSL::Crypto EHEM_CRYPTO_LINK)
endif()
ehem_link_mode(CURL::libcurl EHEM_TRANSPORT_LINK)
message(STATUS "encedo-hem: crypto ${EHEM_CRYPTO_NAME} ${EHEM_CRYPTO_VERSION} "
               "(${EHEM_CRYPTO_LINK}), transport libcurl ${EHEM_CURL_VERSION} "
               "(${EHEM_TRANSPORT_LINK})")
# A static or shared preference that the lookups could not honour is an error,
# never a silent fall-back (REQ-BUILD-006).
if(NOT EHEM_DEPS_LINK STREQUAL "default")
  if(EHEM_DEPS_LINK STREQUAL "static")
    set(_ehem_want static)
  else()
    set(_ehem_want dynamic)
  endif()
  foreach(_dep CRYPTO TRANSPORT)
    if(NOT EHEM_${_dep}_LINK STREQUAL _ehem_want)
      if(_dep STREQUAL "CRYPTO")
        set(_ehem_what "${EHEM_CRYPTO_NAME}")
      else()
        set(_ehem_what "libcurl")
      endif()
      message(FATAL_ERROR "EHEM_DEPS_LINK=${EHEM_DEPS_LINK}: ${_ehem_what} resolved "
                          "to a ${EHEM_${_dep}_LINK} library - no ${_ehem_want} one "
                          "was found where CMake looked")
    endif()
  endforeach()
endif()
# The licensing tripwire (ARCHITECTURE.md §1 and §12 risk 1): any build whose
# wolfSSL is an archive puts wolfSSL inside every binary it links. (OpenSSL 3
# is Apache-2.0: a static OpenSSL is the -static packages' design, REQ-BUILD-009.)
if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl" AND EHEM_CRYPTO_LINK STREQUAL "static")
  message(WARNING "wolfSSL resolves to a STATIC archive in this configuration: "
    "every binary built from it carries wolfSSL, which is licensed GPLv2-or-later "
    "(or commercially by wolfSSL Inc.) - distributing such a binary means "
    "complying with that licence or holding a commercial one (ARCHITECTURE.md "
    "§12 risk 1). No published package of this project links wolfSSL statically, "
    "and the release scripts refuse to package such a build (REQ-BUILD-005).")
endif()
# --- Windows release: hem-tool delay-loads libwolfssl.dll (REQ-TOOL-026) ---
# implements: REQ-TOOL-026 — the delay-load import library of libwolfssl.dll
# (scripts/release/build-deps.sh makes it). When set, hem-tool — and only
# hem-tool — links it plus mingw-w64's delay-load helper instead of the
# ordinary import library and carries the failure hook
# (src/tools/hem-tool/delayload_win.c): a missing DLL becomes hem-tool's own
# message and exit 12 instead of 0xC0000135 before main(). Every other target
# (the SDK DLL, the tests) keeps the ordinary import.
set(EHEM_WOLFSSL_DELAYLOAD_LIB "" CACHE FILEPATH
  "Windows release build: delay-load import library of libwolfssl.dll for hem-tool")
if(EHEM_WOLFSSL_DELAYLOAD_LIB)
  if(NOT EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
    message(FATAL_ERROR "EHEM_WOLFSSL_DELAYLOAD_LIB needs EHEM_CRYPTO_BACKEND=wolfssl")
  endif()
  if(NOT WIN32)
    message(FATAL_ERROR "EHEM_WOLFSSL_DELAYLOAD_LIB applies to Windows builds only")
  endif()
  if(NOT EXISTS "${EHEM_WOLFSSL_DELAYLOAD_LIB}")
    message(FATAL_ERROR "EHEM_WOLFSSL_DELAYLOAD_LIB: no such file ${EHEM_WOLFSSL_DELAYLOAD_LIB}")
  endif()
  if(NOT EHEM_BUILD_TOOL OR NOT EHEM_TOOL_LINK STREQUAL "static" OR
     EHEM_CRYPTO_LINK STREQUAL "static")
    message(FATAL_ERROR "EHEM_WOLFSSL_DELAYLOAD_LIB needs hem-tool carrying the "
                        "SDK (EHEM_TOOL_LINK=static) and a dynamic wolfSSL")
  endif()
  # Resolved per consuming target: one with the EHEM_WOLFSSL_DELAYLOAD
  # property (hem-tool) gets the delay-load library + the helper library,
  # every other one the ordinary import library.
  set(_ehem_delay "$<BOOL:$<TARGET_PROPERTY:EHEM_WOLFSSL_DELAYLOAD>>")
  set(EHEM_CRYPTO_LINK_LIBS
    "$<${_ehem_delay}:${EHEM_WOLFSSL_DELAYLOAD_LIB}>"
    "$<${_ehem_delay}:delayimp>"
    "$<$<NOT:${_ehem_delay}>:PkgConfig::WOLFSSL>")
  # libcurl (static, wolfSSL TLS backend) names the import library in its own
  # link interface — the package records it by path; the same switch there.
  get_target_property(_ehem_curl_real CURL::libcurl ALIASED_TARGET)
  if(NOT _ehem_curl_real)
    set(_ehem_curl_real CURL::libcurl)
  endif()
  get_target_property(_ehem_curl_libs ${_ehem_curl_real} INTERFACE_LINK_LIBRARIES)
  if(_ehem_curl_libs)
    set(_ehem_curl_new "")
    foreach(_l IN LISTS _ehem_curl_libs)
      if(_l MATCHES "(^|[/\\])libwolfssl(\\.dll)?\\.a$" OR _l MATCHES "^(-l)?wolfssl$")
        list(APPEND _ehem_curl_new ${EHEM_CRYPTO_LINK_LIBS})
      else()
        list(APPEND _ehem_curl_new "${_l}")
      endif()
    endforeach()
    set_property(TARGET ${_ehem_curl_real} PROPERTY INTERFACE_LINK_LIBRARIES "${_ehem_curl_new}")
  endif()
  message(STATUS "encedo-hem: hem-tool delay-loads libwolfssl.dll (${EHEM_WOLFSSL_DELAYLOAD_LIB})")
endif()

set(EHEM_LINK_DEFINITIONS "")
if(EHEM_CRYPTO_LINK STREQUAL "static")
  list(APPEND EHEM_LINK_DEFINITIONS EHEM_CRYPTO_LINK_STATIC)
endif()
if(EHEM_TRANSPORT_LINK STREQUAL "static")
  list(APPEND EHEM_LINK_DEFINITIONS EHEM_TRANSPORT_LINK_STATIC)
endif()

# --- vendored third-party (cJSON) -----------------------------------------
# Compiled into an OBJECT library so the vendored sources are held to their
# own (relaxed) warning policy — our -Werror must not gate on code we don't
# own (REQ-BUILD-003). Global hidden visibility + PIC still apply, and
# CJSON_HIDE_SYMBOLS strips cJSON's default __declspec(dllexport) on Windows so
# cJSON never lands in the shared library's export table (REQ-API-006).
add_library(ehem-cjson OBJECT src/vendor/cjson/cJSON.c)
target_compile_definitions(ehem-cjson PRIVATE CJSON_HIDE_SYMBOLS)

# --- library --------------------------------------------------------------
# Compiled twice so the export macro differs (shared → dllexport/visibility,
# static → nothing). The source set is tiny; correctness beats sharing objects.
# The vendored cJSON objects are shared between both (no EHEM_API on them).
set(EHEM_SOURCES
  src/ehem.c
  src/context.c
  src/json.c
  src/transport.c
  src/transport_curl.c
  src/proto_common.c
  src/proto_system.c
  src/proto_auth.c
  src/proto_keymgmt.c
  src/proto_crypto.c
  src/proto_ext.c
  src/proto_logger.c
  src/proto_notify.c
  src/proto_storage.c
  src/keytype.c
  src/crypto_shim_common.c
  src/crypto_shim_${EHEM_CRYPTO_BACKEND}.c
  src/ejwt.c
  src/bip39.c
  src/vendor/bip39/wordlist_english.c)

# The static archive is the installed static variant (EHEM_BUILD_STATIC=ON);
# with the variant off it is still built — privately, never installed, as
# encedo-hem-internal — whenever hem-tool carries the SDK (EHEM_TOOL_LINK=static)
# or the unit tests need the hidden seams. EHEM_STATIC_TARGET names it for the
# in-tree consumers.
if(EHEM_BUILD_STATIC)
  set(EHEM_STATIC_TARGET encedo-hem-static)
elseif(BUILD_TESTING OR (EHEM_BUILD_TOOL AND EHEM_TOOL_LINK STREQUAL "static"))
  set(EHEM_STATIC_TARGET encedo-hem-internal)
else()
  set(EHEM_STATIC_TARGET "")
endif()
set(EHEM_LIBRARY_TARGETS "")
if(EHEM_BUILD_SHARED)
  add_library(encedo-hem SHARED ${EHEM_SOURCES} $<TARGET_OBJECTS:ehem-cjson>)
  list(APPEND EHEM_LIBRARY_TARGETS encedo-hem)
endif()
if(EHEM_STATIC_TARGET)
  add_library(${EHEM_STATIC_TARGET} STATIC ${EHEM_SOURCES} $<TARGET_OBJECTS:ehem-cjson>)
  list(APPEND EHEM_LIBRARY_TARGETS ${EHEM_STATIC_TARGET})
endif()

foreach(_t IN LISTS EHEM_LIBRARY_TARGETS)
  target_include_directories(${_t}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src)      # internal headers (json.h, ...)
  target_compile_definitions(${_t} PRIVATE
    EHEM_VERSION_STRING="${PROJECT_VERSION}"
    ${EHEM_LINK_DEFINITIONS}                # ehem_build_info link modes
    CJSON_HIDE_SYMBOLS)                     # json.c includes cJSON.h; match it
  target_compile_options(${_t} PRIVATE ${EHEM_WARNING_FLAGS})
endforeach()

if(EHEM_BUILD_SHARED)
  # REQ-API-006 when the dependencies are STATIC archives (the release build,
  # REQ-BUILD-005): without this, every libcurl/wolfSSL symbol linked into the
  # shared library would be exported from it. --exclude-libs,ALL keeps archive
  # symbols internal; a no-op when the deps are shared. GNU ld / lld / MinGW ld
  # understand it; other linkers (ld64) simply don't get the flag.
  include(CheckLinkerFlag)
  check_linker_flag(C "-Wl,--exclude-libs,ALL" EHEM_HAVE_EXCLUDE_LIBS)
  if(EHEM_HAVE_EXCLUDE_LIBS)
    target_link_options(encedo-hem PRIVATE "-Wl,--exclude-libs,ALL")
  endif()
  add_library(encedo-hem::encedo-hem ALIAS encedo-hem)
  set_target_properties(encedo-hem PROPERTIES
    OUTPUT_NAME encedo-hem
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  # libcurl and wolfSSL link into the shared lib privately (baked in, not
  # re-exported).
  target_link_libraries(encedo-hem PRIVATE CURL::libcurl ${EHEM_CRYPTO_LINK_LIBS})
  # Only the shared build gets the export-enabling macro; its consumers get the
  # import side (dllimport on Windows; nothing elsewhere) from the target.
  target_compile_definitions(encedo-hem
    PRIVATE   EHEM_BUILDING_SHARED
    INTERFACE EHEM_USING_SHARED)
  # MinGW: with dllexport present, restrict the DLL to those symbols only, and
  # emit a .def file listing the exports. The .def is a stable, machine-readable
  # export list (unlike `objdump -p`, whose format drifts across binutils
  # versions) that the symbol-export check parses on Windows.
  if(WIN32 AND NOT MSVC)
    set_target_properties(encedo-hem PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    target_link_options(encedo-hem PRIVATE
      "-Wl,--output-def,${CMAKE_CURRENT_BINARY_DIR}/libencedo-hem.def")
  endif()
endif()

if(EHEM_STATIC_TARGET)
  # The dependencies are INTERFACE requirements of the static lib so
  # downstream links propagate. The installed export names libcurl as
  # CURL::libcurl, the name every lookup defines (REQ-BUILD-008): curl's own
  # package config makes it an ALIAS of CURL::libcurl_static / _shared, and an
  # export records a bare alias by the aliased name — which FindCURL's module
  # mode never defines. Inside $<1:…> the name is written as it stands.
  target_link_libraries(${EHEM_STATIC_TARGET} PUBLIC
    $<BUILD_INTERFACE:CURL::libcurl> $<INSTALL_INTERFACE:$<1:CURL::libcurl>>
    ${EHEM_CRYPTO_LINK_LIBS})
  if(EHEM_BUILD_STATIC)
    add_library(encedo-hem::encedo-hem-static ALIAS encedo-hem-static)
    set_target_properties(encedo-hem-static PROPERTIES OUTPUT_NAME encedo-hem)
  else()
    set_target_properties(${EHEM_STATIC_TARGET} PROPERTIES
      OUTPUT_NAME encedo-hem-internal)
  endif()
endif()

# --- install / export -----------------------------------------------------
# Only the variants built (REQ-BUILD-006); the private archive never ships.
set(_ehem_install_targets "")
if(EHEM_BUILD_SHARED)
  list(APPEND _ehem_install_targets encedo-hem)
endif()
if(EHEM_BUILD_STATIC)
  list(APPEND _ehem_install_targets encedo-hem-static)
endif()
install(TARGETS ${_ehem_install_targets}
  EXPORT encedo-hem-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT encedo-hem-targets
  NAMESPACE encedo-hem::
  FILE encedo-hem-targets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)

# implements: REQ-BUILD-008 — the installed consumer interface: the package
# config resolves what the static library's link interface names (libcurl and
# the crypto library) before importing the targets; the shared library carries
# both privately, so a shared-only install has nothing to resolve.
if(EHEM_BUILD_STATIC AND EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
  set(EHEM_CONFIG_DEPENDENCIES [=[
# The static library's link interface names libcurl and wolfSSL: resolve both
# before importing the targets (the shared library carries them privately).
find_dependency(CURL)
# wolfSSL through pkg-config, as in the SDK's own build (the distributions ship
# no CMake package for it): the imported target PkgConfig::WOLFSSL.
find_dependency(PkgConfig)
pkg_check_modules(WOLFSSL QUIET IMPORTED_TARGET wolfssl)
if(NOT TARGET PkgConfig::WOLFSSL)
  set(encedo-hem_FOUND FALSE)
  set(encedo-hem_NOT_FOUND_MESSAGE "encedo-hem: the static library needs wolfSSL, and pkg-config finds no wolfssl.pc - point PKG_CONFIG_PATH at it")
  return()
endif()]=])
elseif(EHEM_BUILD_STATIC)
  set(EHEM_CONFIG_DEPENDENCIES [=[
# The static library's link interface names libcurl and OpenSSL's libcrypto:
# resolve both before importing the targets (the shared library carries them
# privately). For OpenSSL's static archives set OPENSSL_USE_STATIC_LIBS first.
find_dependency(CURL)
find_dependency(OpenSSL 3)]=])
else()
  set(EHEM_CONFIG_DEPENDENCIES
    "# Shared library only: libcurl and ${EHEM_CRYPTO_NAME} are linked into it privately.")
endif()

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/encedo-hem-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)

# encedo-hem.pc (REQ-BUILD-008): relocatable — its paths are relative to its own
# directory (${pcfiledir}) unless an install directory is absolute.
# Requires.private names the packages `pkg-config --static` adds for the static
# library; `-lencedo-hem` alone is the shared one wherever it is installed.
if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
  set(EHEM_PC_REQUIRES_PRIVATE "libcurl wolfssl")
else()
  set(EHEM_PC_REQUIRES_PRIVATE "libcurl libcrypto")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
  set(EHEM_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(EHEM_PC_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(EHEM_PC_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
else()
  # ${pcfiledir} = <prefix>/<libdir>/pkgconfig: one ".." per component.
  string(REGEX MATCHALL "[^/]+" _ehem_pc_parts "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  set(EHEM_PC_PREFIX "\${pcfiledir}")
  foreach(_ehem_pc_part IN LISTS _ehem_pc_parts)
    string(APPEND EHEM_PC_PREFIX "/..")
  endforeach()
  set(EHEM_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
  set(EHEM_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/encedo-hem.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# --- hem-tool CLI ---------------------------------------------------------
# Thin consumer of the PUBLIC API only (REQ-TOOL-001). By default it links the
# static SDK so the tool carries the SDK itself (no libencedo-hem.so lookup at
# run time); EHEM_TOOL_LINK=shared links libencedo-hem instead (REQ-BUILD-006).
# The SDK's own dependencies, curl and wolfSSL, come through the SDK target's
# link interface and are dynamic or not per the build — the release build links
# wolfSSL dynamically (REQ-BUILD-005).
if(EHEM_BUILD_TOOL)
  # The tool's command code lives in a small static lib so the tool binary and
  # the unit/disruptive tests share ONE copy of it. Like the rest of the tool it
  # uses only the public API. hem-tool-core always sits on the static SDK — the
  # unit tests drive it through the hidden seams; with EHEM_TOOL_LINK=shared the
  # executable gets its own copy built against the shared SDK.
  set(HEM_TOOL_CORE_SOURCES
    src/tools/hem-tool/cert_install.c
    src/tools/hem-tool/ext_cmd.c
    src/tools/hem-tool/init_cmd.c
    src/tools/hem-tool/keys.c
    src/tools/hem-tool/keytypes.c
    src/tools/hem-tool/logs.c
    src/tools/hem-tool/prompt.c
    src/tools/hem-tool/random.c
    src/tools/hem-tool/recover.c
    src/tools/hem-tool/recovery.c
    src/tools/hem-tool/registry.c
    src/tools/hem-tool/selftest.c
    src/tools/hem-tool/sign.c
    src/tools/hem-tool/startup.c
    src/tools/hem-tool/tool_auth.c
    src/tools/hem-tool/wipe.c
    src/tools/hem-tool/wizard.c
    # Vendored qrcodegen (REQ-TOOL-016): TOOL-ONLY — never part of the SDK
    # library; the export/header gates prove the containment.
    src/tools/hem-tool/vendor/qrcodegen/qrcodegen.c)
  set(_ehem_tool_cores "")
  if(EHEM_STATIC_TARGET)
    add_library(hem-tool-core STATIC ${HEM_TOOL_CORE_SOURCES})
    target_link_libraries(hem-tool-core PUBLIC ${EHEM_STATIC_TARGET})
    list(APPEND _ehem_tool_cores hem-tool-core)
  endif()
  if(EHEM_TOOL_LINK STREQUAL "shared")
    add_library(hem-tool-core-shared STATIC ${HEM_TOOL_CORE_SOURCES})
    target_link_libraries(hem-tool-core-shared PUBLIC encedo-hem::encedo-hem)
    list(APPEND _ehem_tool_cores hem-tool-core-shared)
    set(_ehem_tool_core hem-tool-core-shared)
  else()
    set(_ehem_tool_core hem-tool-core)
  endif()
  foreach(_t IN LISTS _ehem_tool_cores)
    target_include_directories(${_t} PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/hem-tool)
    target_compile_options(${_t} PRIVATE ${EHEM_WARNING_FLAGS})
  endforeach()

  add_executable(hem-tool src/tools/hem-tool/main.c)
  target_link_libraries(hem-tool PRIVATE ${_ehem_tool_core})
  target_compile_options(hem-tool PRIVATE ${EHEM_WARNING_FLAGS})
  if(EHEM_WOLFSSL_DELAYLOAD_LIB)
    # REQ-TOOL-026: the delay-load import + its failure hook, hem-tool only.
    set_property(TARGET hem-tool PROPERTY EHEM_WOLFSSL_DELAYLOAD ON)
    target_sources(hem-tool PRIVATE src/tools/hem-tool/delayload_win.c)
    target_compile_definitions(hem-tool PRIVATE HEM_TOOL_DELAYLOAD
      EHEM_WOLFSSL_COMPANION_VERSION="${WOLFSSL_VERSION}")
  endif()
  install(TARGETS hem-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# --- tests ----------------------------------------------------------------
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
