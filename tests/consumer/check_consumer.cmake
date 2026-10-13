# check_consumer.cmake — the consumer gate: install the build into a scratch
# prefix, check the installed interface files, then configure, build and run
# tests/consumer against that install — find_package and pkg-config, shared and
# static, for the variants the build carries.
#
# verifies: REQ-BUILD-008 (the installed package config resolves the crypto
#           dependency for the static target and exports only the variants
#           built; the shared target carries EHEM_USING_SHARED; encedo-hem.pc
#           is installed, relocatable, with Requires.private; a consumer builds
#           and runs via find_package AND pkg-config for every variant built;
#           no auto-import note on Windows)
#
# Run as a CTest (label `unit`, tests/CMakeLists.txt):
#   cmake -DEHEM_BUILD_DIR=<build> -DEHEM_SCRATCH=<dir> -DEHEM_CONSUMER_SRC=<src>
#         -DEHEM_VERSION=<x.y.z> -DEHEM_LIBDIR=<libdir> -DEHEM_SHARED=ON|OFF
#         -DEHEM_STATIC=ON|OFF -DEHEM_GENERATOR=<gen> [-DEHEM_MAKE_PROGRAM=<path>]
#         -DEHEM_C_COMPILER=<cc> [-DEHEM_BUILD_TYPE=<type>]
#         [-DEHEM_PREFIX_PATH=<a|b>] [-DEHEM_SANITIZE=ON]
#         [-DEHEM_CRYPTO_BACKEND=wolfssl|openssl]
#         -P check_consumer.cmake
# EHEM_PREFIX_PATH is the build's CMAKE_PREFIX_PATH with `|` for `;`: the
# consumer resolves the SDK's dependencies where the build did (the release
# prefix, for package.sh).

cmake_minimum_required(VERSION 3.20)

foreach(_v EHEM_BUILD_DIR EHEM_SCRATCH EHEM_CONSUMER_SRC EHEM_VERSION EHEM_LIBDIR
           EHEM_GENERATOR EHEM_C_COMPILER)
  if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
    message(FATAL_ERROR "check_consumer.cmake: ${_v} not set")
  endif()
endforeach()

if(NOT EHEM_CRYPTO_BACKEND)
  set(EHEM_CRYPTO_BACKEND wolfssl)
endif()
if(EHEM_CRYPTO_BACKEND STREQUAL "wolfssl")
  set(_crypto_lookup "pkg_check_modules(WOLFSSL QUIET IMPORTED_TARGET wolfssl)")
  set(_crypto_pc "wolfssl")
else()
  set(_crypto_lookup "find_dependency(OpenSSL 3)")
  set(_crypto_pc "libcrypto")
endif()

set(_prefix "${EHEM_SCRATCH}/prefix")
set(_build "${EHEM_SCRATCH}/build")
if(CMAKE_HOST_WIN32)
  set(_sep ";")
  set(_exe ".exe")
else()
  set(_sep ":")
  set(_exe "")
endif()

# step() forwards its command as a list: a `;` inside one argument (a path list)
# must travel escaped — esc() does that. And no argument may end in `\`: before
# the list's next `;` that reads as an escaped `;`, gluing the next argument on
# (MSYS2's PATH ends in ...\WindowsPowerShell\v1.0\). So the native path lists,
# PATH and PKG_CONFIG_PATH, reach the children through this script's
# environment, and the prefix path goes in forward slashes.
function(esc out value)
  string(REPLACE ";" "\\;" _v "${value}")
  set(${out} "${_v}" PARENT_SCOPE)
endfunction()

function(step what)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${what} failed (${_rc}):\n${_out}\n${_err}")
  endif()
  set(_step_out "${_out}${_err}" PARENT_SCOPE)
endfunction()

# Functions, not macros: a macro would expand `${...}` inside the expected
# text (the .pc's literal ${pcfiledir}) a second time. `_text` is the caller's.
function(expect_in file text)
  string(FIND "${_text}" "${text}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "'${text}' not in ${file}:\n${_text}")
  endif()
endfunction()

function(expect_not_in file text)
  string(FIND "${_text}" "${text}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "'${text}' unexpectedly in ${file}:\n${_text}")
  endif()
endfunction()

# --- install --------------------------------------------------------------
file(REMOVE_RECURSE "${EHEM_SCRATCH}")
step("cmake --install" ${CMAKE_COMMAND} --install "${EHEM_BUILD_DIR}" --prefix "${_prefix}")

# --- the installed interface files ----------------------------------------
set(_cmakedir "${_prefix}/${EHEM_LIBDIR}/cmake/encedo-hem")
set(_pcdir "${_prefix}/${EHEM_LIBDIR}/pkgconfig")

file(READ "${_cmakedir}/encedo-hem-config.cmake" _text)
if(EHEM_STATIC)
  expect_in("encedo-hem-config.cmake" "find_dependency(CURL)")
  expect_in("encedo-hem-config.cmake" "${_crypto_lookup}")
else()
  expect_not_in("encedo-hem-config.cmake" "find_dependency(")
endif()

file(READ "${_cmakedir}/encedo-hem-targets.cmake" _text)
if(EHEM_SHARED)
  expect_in("encedo-hem-targets.cmake" "add_library(encedo-hem::encedo-hem SHARED IMPORTED)")
  # The shared target's consumers get the import side of EHEM_API.
  string(REGEX MATCH
    "set_target_properties\\(encedo-hem::encedo-hem PROPERTIES[^)]*INTERFACE_COMPILE_DEFINITIONS \"EHEM_USING_SHARED\""
    _m "${_text}")
  if(NOT _m)
    message(FATAL_ERROR "the exported shared target lacks INTERFACE_COMPILE_DEFINITIONS EHEM_USING_SHARED:\n${_text}")
  endif()
else()
  expect_not_in("encedo-hem-targets.cmake" "encedo-hem::encedo-hem SHARED")
endif()
if(EHEM_STATIC)
  expect_in("encedo-hem-targets.cmake" "add_library(encedo-hem::encedo-hem-static STATIC IMPORTED)")
  # libcurl by the name every lookup defines — never an aliased
  # CURL::libcurl_static / _shared from curl's own package config.
  expect_in("encedo-hem-targets.cmake" "CURL::libcurl")
  expect_not_in("encedo-hem-targets.cmake" "CURL::libcurl_")
else()
  expect_not_in("encedo-hem-targets.cmake" "encedo-hem::encedo-hem-static")
endif()

file(READ "${_pcdir}/encedo-hem.pc" _text)
expect_in("encedo-hem.pc" "prefix=\${pcfiledir}/")       # relocatable
expect_in("encedo-hem.pc" "Version: ${EHEM_VERSION}\n")
expect_in("encedo-hem.pc" "Requires.private: libcurl ${_crypto_pc}\n")
expect_in("encedo-hem.pc" "Cflags: -I\${includedir}\n")
expect_in("encedo-hem.pc" "Libs: -L\${libdir} -lencedo-hem\n")

# --- configure, build and run the consumer --------------------------------
set(_pp "${_prefix}")
if(EHEM_PREFIX_PATH)
  string(REPLACE "|" ";" _parent "${EHEM_PREFIX_PATH}")
  string(APPEND _pp ";${_parent}")
endif()
if(CMAKE_HOST_WIN32)
  string(REPLACE "\\" "/" _pp "${_pp}")
endif()
set(_args -S "${EHEM_CONSUMER_SRC}" -B "${_build}" -G "${EHEM_GENERATOR}"
  "-DCMAKE_C_COMPILER=${EHEM_C_COMPILER}"
  "-DCONSUMER_PREFIX=${_prefix}"
  -DCONSUMER_SHARED=${EHEM_SHARED} -DCONSUMER_STATIC=${EHEM_STATIC})
if(EHEM_MAKE_PROGRAM)
  list(APPEND _args "-DCMAKE_MAKE_PROGRAM=${EHEM_MAKE_PROGRAM}")
endif()
if(EHEM_BUILD_TYPE)
  list(APPEND _args "-DCMAKE_BUILD_TYPE=${EHEM_BUILD_TYPE}")
endif()
if(EHEM_SANITIZE)
  # The installed SDK is instrumented; so must its consumer be.
  list(APPEND _args
    "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined")
endif()
if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
  set(ENV{PKG_CONFIG_PATH} "${_pcdir}${_sep}$ENV{PKG_CONFIG_PATH}")
else()
  set(ENV{PKG_CONFIG_PATH} "${_pcdir}")
endif()
esc(_pp "${_pp}")
step("consumer configure" ${CMAKE_COMMAND} "-DCMAKE_PREFIX_PATH=${_pp}" ${_args})
string(REGEX MATCH "consumer_pc_static: [^\n]*" _form "${_step_out}")
step("consumer build" ${CMAKE_COMMAND} --build "${_build}")
# Windows: the shared consumers reach the DLL through dllimport, never through
# the linker's auto-import fallback.
string(FIND "${_step_out}" "auto-import" _pos)
if(NOT _pos EQUAL -1)
  message(FATAL_ERROR "the consumer link used auto-import:\n${_step_out}")
endif()

set(_runs "")
if(EHEM_SHARED)
  list(APPEND _runs "consumer_cmake_shared|EHEM_USING_SHARED" "consumer_pc_shared|no EHEM_USING_SHARED")
endif()
if(EHEM_STATIC)
  list(APPEND _runs "consumer_cmake_static|no EHEM_USING_SHARED" "consumer_pc_static|no EHEM_USING_SHARED")
endif()
if(CMAKE_HOST_WIN32)
  # The DLL sits in the install's bin/ — first in the search order.
  set(ENV{PATH} "${_prefix}/bin;$ENV{PATH}")
endif()
foreach(_run IN LISTS _runs)
  string(REPLACE "|" ";" _run "${_run}")
  list(GET _run 0 _name)
  list(GET _run 1 _define)
  step("${_name}" "${_build}/${_name}${_exe}")
  if(NOT _step_out STREQUAL "encedo-hem ${EHEM_VERSION}, ${_define}\n")
    message(FATAL_ERROR "${_name}: printed '${_step_out}', expected "
                        "'encedo-hem ${EHEM_VERSION}, ${_define}'")
  endif()
  message(STATUS "${_name}: OK (${_define})")
endforeach()
if(_form)
  message(STATUS "${_form}")
endif()
message(STATUS "consumer gate OK: shared=${EHEM_SHARED} static=${EHEM_STATIC}")
