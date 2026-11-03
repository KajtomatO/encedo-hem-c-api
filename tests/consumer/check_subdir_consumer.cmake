# check_subdir_consumer.cmake — the out-of-tree consumer gate (REQ-BUILD-011):
# configure, build and run tests/consumer/subdir — a scratch project that adds
# THIS source tree with add_subdirectory(... EXCLUDE_FROM_ALL) after shadowing
# BUILD_TESTING and EHEM_BUILD_SHARED OFF and EHEM_BUILD_TEST_SUPPORT ON (normal
# variables, CMP0077) — and check that the SDK brought no test and no program
# of its own into that build.
#
# verifies: REQ-BUILD-011
#
# Run as a CTest (label `unit`, tests/CMakeLists.txt):
#   cmake -DEHEM_SDK_SOURCE_DIR=<repo> -DEHEM_SCRATCH=<dir> -DEHEM_CONSUMER_SRC=<src>
#         -DEHEM_GENERATOR=<gen> [-DEHEM_MAKE_PROGRAM=<path>] -DEHEM_C_COMPILER=<cc>
#         [-DEHEM_BUILD_TYPE=<type>] [-DEHEM_PREFIX_PATH=<a|b>] [-DEHEM_SANITIZE=ON]
#         [-DEHEM_CRYPTO_BACKEND=wolfssl|openssl] [-DEHEM_DEPS_LINK=default|static|shared]
#         -P check_subdir_consumer.cmake
# EHEM_PREFIX_PATH is the build's CMAKE_PREFIX_PATH with `|` for `;`: the
# consumer's SDK resolves libcurl and the crypto library where this build did
# (the release prefix, for package.sh). Path lists travel as in
# check_consumer.cmake: escaped `;`, forward slashes on Windows.

cmake_minimum_required(VERSION 3.20)

foreach(_v IN ITEMS EHEM_SDK_SOURCE_DIR EHEM_SCRATCH EHEM_CONSUMER_SRC EHEM_GENERATOR
                    EHEM_C_COMPILER)
  if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
    message(FATAL_ERROR "check_subdir_consumer.cmake: ${_v} not set")
  endif()
endforeach()
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
else()
  set(_exe "")
endif()
set(_build "${EHEM_SCRATCH}/build")

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

# --- configure + build the consumer, the SDK inside it ------------------------
file(REMOVE_RECURSE "${EHEM_SCRATCH}")
set(_sdk "${EHEM_SDK_SOURCE_DIR}")
set(_pp "")
if(EHEM_PREFIX_PATH)
  string(REPLACE "|" ";" _pp "${EHEM_PREFIX_PATH}")
endif()
if(CMAKE_HOST_WIN32)
  string(REPLACE "\\" "/" _sdk "${_sdk}")
  string(REPLACE "\\" "/" _pp "${_pp}")
endif()
set(_args -S "${EHEM_CONSUMER_SRC}" -B "${_build}" -G "${EHEM_GENERATOR}"
  "-DCMAKE_C_COMPILER=${EHEM_C_COMPILER}"
  "-DEHEM_SDK_SOURCE_DIR=${_sdk}")
if(EHEM_MAKE_PROGRAM)
  list(APPEND _args "-DCMAKE_MAKE_PROGRAM=${EHEM_MAKE_PROGRAM}")
endif()
if(EHEM_BUILD_TYPE)
  list(APPEND _args "-DCMAKE_BUILD_TYPE=${EHEM_BUILD_TYPE}")
endif()
if(EHEM_CRYPTO_BACKEND)
  list(APPEND _args "-DEHEM_CRYPTO_BACKEND=${EHEM_CRYPTO_BACKEND}")
endif()
if(EHEM_DEPS_LINK)
  list(APPEND _args "-DEHEM_DEPS_LINK=${EHEM_DEPS_LINK}")
endif()
if(EHEM_SANITIZE)
  list(APPEND _args -DEHEM_SANITIZE=ON)
endif()
if(_pp)
  esc(_pp "${_pp}")
  list(APPEND _args "-DCMAKE_PREFIX_PATH=${_pp}")
endif()
step("subdir consumer configure" ${CMAKE_COMMAND} ${_args})
step("subdir consumer build" ${CMAKE_COMMAND} --build "${_build}")

# --- the SDK brought nothing of its own but the libraries ---------------------
# No test: with BUILD_TESTING shadowed OFF the SDK registers none and the
# consumer declares none, so ctest finds no test (or no test file at all).
if(NOT CMAKE_CTEST_COMMAND)
  get_filename_component(_bindir "${CMAKE_COMMAND}" DIRECTORY)
  set(CMAKE_CTEST_COMMAND "${_bindir}/ctest${_exe}")
endif()
execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${_build}" -N
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if("${_out}${_err}" MATCHES "Test +#[0-9]+:")
  message(FATAL_ERROR "the consumer's build carries tests of the SDK:\n${_out}${_err}")
endif()
# No program target: the generator lists every target it wrote. hem-tool left
# with M13; the probe and the test/check executables belong to BUILD_TESTING.
set(_tdirs "${_build}/CMakeFiles/TargetDirectories.txt")
if(NOT EXISTS "${_tdirs}")
  message(FATAL_ERROR "no ${_tdirs} — a generator this gate does not know")
endif()
file(READ "${_tdirs}" _targets)
if(_targets MATCHES "/CMakeFiles/(hem-tool|tls-probe|test_[a-z]|check_[a-z])")
  message(FATAL_ERROR "the SDK brought a test or program target into the consumer's build:\n${_targets}")
endif()
string(REGEX MATCHALL "/CMakeFiles/[^/\n]+\\.dir" _names "${_targets}")
list(TRANSFORM _names REPLACE "/CMakeFiles/(.*)\\.dir" "\\1")
list(REMOVE_DUPLICATES _names)     # CMake's per-directory utility targets
list(SORT _names)
message(STATUS "targets in the consumer's build: ${_names}")

# --- run it -------------------------------------------------------------------
step("subdir_consumer run" "${_build}/subdir_consumer${_exe}")
if(NOT _step_out MATCHES "^subdir_consumer: OK")
  message(FATAL_ERROR "subdir_consumer printed:\n${_step_out}")
endif()
message(STATUS "${_step_out}")
message(STATUS "subdir consumer gate OK")
