# check_tests_backend_free.cmake — no test knows which crypto backend the SDK
# was built with: under tests/, only the per-backend reference module
# tests/support/crypto_ref_<backend>.c includes a wolfSSL or OpenSSL header.
#
# verifies: REQ-BUILD-007 (nothing outside the shim and the test-support
#           layer's crypto_ref knows the backend — the grep criterion)
#
# Run as a CTest (label `unit`):
#   cmake -DEHEM_TESTS_DIR=<repo>/tests -P check_tests_backend_free.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED EHEM_TESTS_DIR)
  message(FATAL_ERROR "EHEM_TESTS_DIR not set")
endif()

file(GLOB_RECURSE _files "${EHEM_TESTS_DIR}/*.c" "${EHEM_TESTS_DIR}/*.h")
set(_bad "")
set(_allowed "")
foreach(_f IN LISTS _files)
  file(STRINGS "${_f}" _hits
       REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](wolfssl|openssl)/")
  if(_hits)
    get_filename_component(_name "${_f}" NAME)
    get_filename_component(_dir "${_f}" DIRECTORY)
    if(_dir STREQUAL "${EHEM_TESTS_DIR}/support" AND
       _name MATCHES "^crypto_ref_[a-z]+\\.c$")
      list(APPEND _allowed "${_name}")
    else()
      list(APPEND _bad "${_f}")
    endif()
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR
    "a test includes a crypto backend header - route it through "
    "tests/support/crypto_ref.h: ${_bad}")
endif()
# The scan itself works: both reference modules do include their library.
foreach(_ref crypto_ref_wolfssl.c crypto_ref_openssl.c)
  if(NOT _ref IN_LIST _allowed)
    message(FATAL_ERROR "the scan found no backend include in tests/support/${_ref}")
  endif()
endforeach()

message(STATUS "tests are crypto-backend-free (backend headers only in: ${_allowed})")
