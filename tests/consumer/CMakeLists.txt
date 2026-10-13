# tests/consumer — a minimal application of an INSTALLED encedo-hem
# (REQ-BUILD-008). Not part of the SDK's own build: check_consumer.cmake installs
# the build into a scratch prefix and configures this project against it, with
# one executable per way in, for the variants that install carries:
#
#   consumer_cmake_shared  find_package -> encedo-hem::encedo-hem
#   consumer_cmake_static  find_package -> encedo-hem::encedo-hem-static
#   consumer_pc_shared     pkg-config encedo-hem (-lencedo-hem: the shared library)
#   consumer_pc_static     the archive + `pkg-config --static` — the standard
#                          static link — where every library of that closure can
#                          be linked here; elsewhere the archive + the --libs of
#                          the .pc's Requires.private packages, i.e. the
#                          dependencies as the shared libraries they are there
#                          (Ubuntu's libcurl lists private libraries that have
#                          no linkable file). The form used goes to the gate's log.
#
# supports: REQ-BUILD-008

cmake_minimum_required(VERSION 3.20)
project(encedo-hem-consumer C)

option(CONSUMER_SHARED "the install carries the shared library" ON)
option(CONSUMER_STATIC "the install carries the static library" ON)
set(CONSUMER_PREFIX "" CACHE PATH "the scratch install both lookups must resolve to")

# Both lookups must land in the scratch install, never in another copy of the
# SDK on this machine.
function(consumer_require_in_prefix what path)
  cmake_path(IS_PREFIX CONSUMER_PREFIX "${path}" NORMALIZE _inside)
  if(NOT _inside)
    message(FATAL_ERROR "${what} resolved to ${path}, outside ${CONSUMER_PREFIX}")
  endif()
endfunction()

find_package(encedo-hem CONFIG REQUIRED)
consumer_require_in_prefix("find_package(encedo-hem)" "${encedo-hem_DIR}")
find_package(PkgConfig REQUIRED)
pkg_check_modules(EHEM_PC REQUIRED encedo-hem)
consumer_require_in_prefix("pkg-config encedo-hem" "${EHEM_PC_LIBDIR}")

if(CONSUMER_SHARED)
  add_executable(consumer_cmake_shared consumer.c)
  target_link_libraries(consumer_cmake_shared PRIVATE encedo-hem::encedo-hem)

  pkg_check_modules(EHEM_PC_SHARED REQUIRED IMPORTED_TARGET encedo-hem)
  add_executable(consumer_pc_shared consumer.c)
  target_link_libraries(consumer_pc_shared PRIVATE PkgConfig::EHEM_PC_SHARED)

  # MinGW: the DLL's consumers must link without the linker's auto-import,
  # which x86_64 ld applies silently (no note) — a reference that needs it
  # fails the link here instead.
  if(MINGW)
    target_link_options(consumer_cmake_shared PRIVATE -Wl,--disable-auto-import)
    target_link_options(consumer_pc_shared PRIVATE -Wl,--disable-auto-import)
  endif()
endif()

if(CONSUMER_STATIC)
  add_executable(consumer_cmake_static consumer.c)
  target_link_libraries(consumer_cmake_static PRIVATE encedo-hem::encedo-hem-static)

  # The archive by its file name: where the shared library is installed too,
  # -lencedo-hem resolves to that one.
  find_library(EHEM_PC_ARCHIVE
    NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}encedo-hem${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${EHEM_PC_LIBDIR} NO_DEFAULT_PATH)
  if(NOT EHEM_PC_ARCHIVE)
    message(FATAL_ERROR "no encedo-hem archive in the .pc's libdir ${EHEM_PC_LIBDIR}")
  endif()

  # `pkg-config --static`: system and compiler-runtime libraries link by name
  # (a CMake-built libcurl.pc lists -lgcc -lgcc_s -lc, and on MinGW -lmingw32
  # -lmoldname -lmingwex); every other library of the closure must have a
  # linkable file (shared or archive) here.
  set(_system m pthread dl rt c resolv gcc gcc_s gcc_eh
      mingw32 moldname mingwex mingwthrd msvcrt ucrt ucrtbase winpthread
      ws2_32 crypt32 bcrypt advapi32 secur32 iphlpapi wldap32 normaliz user32
      kernel32 gdi32 ole32 shell32 rpcrt4 userenv)
  set(_closure "")
  set(_missing "")
  foreach(_lib IN LISTS EHEM_PC_STATIC_LIBRARIES)
    if(_lib STREQUAL "encedo-hem")
      continue()
    elseif(_lib IN_LIST _system)
      list(APPEND _closure ${_lib})
      continue()
    endif()
    string(MAKE_C_IDENTIFIER "CONSUMER_LIB_${_lib}" _var)
    find_library(${_var} NAMES ${_lib} HINTS ${EHEM_PC_STATIC_LIBRARY_DIRS})
    if(${_var})
      list(APPEND _closure "${${_var}}")
    else()
      list(APPEND _missing ${_lib})
    endif()
  endforeach()

  add_executable(consumer_pc_static consumer.c)
  target_include_directories(consumer_pc_static PRIVATE ${EHEM_PC_STATIC_INCLUDE_DIRS})
  target_compile_options(consumer_pc_static PRIVATE ${EHEM_PC_STATIC_CFLAGS_OTHER})
  if(NOT _missing)
    target_link_libraries(consumer_pc_static PRIVATE "${EHEM_PC_ARCHIVE}" ${_closure})
    set(_form "pkg-config --static closure")
  else()
    execute_process(
      COMMAND ${PKG_CONFIG_EXECUTABLE} --print-requires-private encedo-hem
      OUTPUT_VARIABLE _private RESULT_VARIABLE _rc OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "[ \t\r\n]+" ";" _private "${_private}")
    if(NOT _rc EQUAL 0 OR NOT _private)
      message(FATAL_ERROR "pkg-config --print-requires-private encedo-hem: no packages")
    endif()
    pkg_check_modules(EHEM_PC_PRIVATE REQUIRED IMPORTED_TARGET ${_private})
    target_link_libraries(consumer_pc_static PRIVATE
      "${EHEM_PC_ARCHIVE}" PkgConfig::EHEM_PC_PRIVATE)
    list(REMOVE_DUPLICATES _missing)
    string(REPLACE ";" " " _missing "${_missing}")
    string(REPLACE ";" " " _private "${_private}")
    set(_form "archive + the shared ${_private} (the --static closure is not linkable here: no file for ${_missing})")
  endif()
  message(STATUS "consumer_pc_static: ${_form}")
endif()
