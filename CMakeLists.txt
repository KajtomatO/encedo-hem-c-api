# encedo-hem-c-api — the Encedo HEM C SDK.
# implements: REQ-BUILD-001
#
# Cross-platform build (Linux GCC/Clang, Windows MinGW/MSYS2) producing static
# and shared `encedo-hem` libraries plus, later, the hem-tool executable.
# See ARCHITECTURE.md §1 (fixed decisions) and §10 (directory layout).

cmake_minimum_required(VERSION 3.20)

project(encedo-hem
  VERSION 0.1.0
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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# HTTPS transport dependency (REQ-NET-002). The FindCURL module provides the
# imported target CURL::libcurl; no cURL type appears in public headers.
find_package(CURL REQUIRED)

# Crypto primitives for the auth flow (REQ-AUTH-001): wolfCrypt via wolfSSL,
# used from M2 on (PBKDF2-HMAC-SHA256 / HMAC-SHA256 / X25519). The dev distros
# (Debian/Ubuntu apt, MSYS2 pacman) ship a pkg-config file but no CMake package
# config, so discover via pkg-config into the imported target PkgConfig::WOLFSSL.
# Like libcurl, no wolfSSL type appears in public headers (crypto_shim contains
# it), so it links privately into the shared library.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WOLFSSL REQUIRED IMPORTED_TARGET wolfssl)

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
  src/proto_logger.c
  src/proto_storage.c
  src/keytype.c
  src/crypto_shim.c
  src/ejwt.c)

add_library(encedo-hem        SHARED ${EHEM_SOURCES} $<TARGET_OBJECTS:ehem-cjson>)
add_library(encedo-hem-static STATIC ${EHEM_SOURCES} $<TARGET_OBJECTS:ehem-cjson>)
add_library(encedo-hem::encedo-hem        ALIAS encedo-hem)
add_library(encedo-hem::encedo-hem-static ALIAS encedo-hem-static)

foreach(_t encedo-hem encedo-hem-static)
  target_include_directories(${_t}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src)      # internal headers (json.h, ...)
  target_compile_definitions(${_t} PRIVATE
    EHEM_VERSION_STRING="${PROJECT_VERSION}"
    CJSON_HIDE_SYMBOLS)                     # json.c includes cJSON.h; match it
  target_compile_options(${_t} PRIVATE ${EHEM_WARNING_FLAGS})
  set_target_properties(${_t} PROPERTIES OUTPUT_NAME encedo-hem)
endforeach()

# libcurl and wolfSSL link into the shared lib privately (baked in, not
# re-exported), but are INTERFACE requirements of the static lib so downstream
# links propagate.
target_link_libraries(encedo-hem        PRIVATE CURL::libcurl PkgConfig::WOLFSSL)
target_link_libraries(encedo-hem-static PUBLIC  CURL::libcurl PkgConfig::WOLFSSL)

# Only the shared build gets the export-enabling macro.
target_compile_definitions(encedo-hem PRIVATE EHEM_BUILDING_SHARED)
set_target_properties(encedo-hem PROPERTIES
  VERSION   ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

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

# --- install / export -----------------------------------------------------
install(TARGETS encedo-hem encedo-hem-static
  EXPORT encedo-hem-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT encedo-hem-targets
  NAMESPACE encedo-hem::
  FILE encedo-hem-targets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)

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

# --- hem-tool CLI ---------------------------------------------------------
# Thin consumer of the PUBLIC API only (REQ-TOOL-001); links the static lib so
# the tool is self-contained (no runtime .so lookup), with curl coming through
# the static lib's interface link. Building it can be turned off for a
# library-only consumer.
option(EHEM_BUILD_TOOL "Build the hem-tool CLI" ON)
if(EHEM_BUILD_TOOL)
  # The cert-install orchestration (REQ-TOOL-003) lives in a small static lib so
  # the tool binary and the unit/disruptive tests share ONE copy of it. Like the
  # rest of the tool it uses only the public API (through the static SDK).
  add_library(hem-tool-core STATIC
    src/tools/hem-tool/cert_install.c
    src/tools/hem-tool/keys.c
    src/tools/hem-tool/logs.c
    src/tools/hem-tool/random.c
    src/tools/hem-tool/recover.c
    src/tools/hem-tool/selftest.c
    src/tools/hem-tool/sign.c)
  target_link_libraries(hem-tool-core PUBLIC encedo-hem::encedo-hem-static)
  target_include_directories(hem-tool-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/hem-tool)
  target_compile_options(hem-tool-core PRIVATE ${EHEM_WARNING_FLAGS})

  add_executable(hem-tool src/tools/hem-tool/main.c)
  target_link_libraries(hem-tool PRIVATE hem-tool-core)
  target_compile_options(hem-tool PRIVATE ${EHEM_WARNING_FLAGS})
  install(TARGETS hem-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# --- tests ----------------------------------------------------------------
include(CTest)               # defines BUILD_TESTING (default ON), enables CTest
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
