# check_public_headers.cmake — assert the public headers stay free of the
# private third-party dependencies (libcurl and wolfSSL).
#
# verifies: REQ-NET-002 (libcurl never appears in include/ehem/, so a consumer
#           can compile against the SDK without libcurl dev headers)
# verifies: REQ-AUTH-001 (the crypto shim keeps wolfSSL contained; no wolfSSL
#           header or type leaks into include/ehem/)
#
# Run as a CTest (label `unit`):
#   cmake -DEHEM_INCLUDE_DIR=<repo>/include -P check_public_headers.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED EHEM_INCLUDE_DIR)
  message(FATAL_ERROR "EHEM_INCLUDE_DIR not set")
endif()

file(GLOB_RECURSE _headers "${EHEM_INCLUDE_DIR}/*.h")
if(NOT _headers)
  message(FATAL_ERROR "no public headers found under ${EHEM_INCLUDE_DIR}")
endif()

set(_bad "")
foreach(_h IN LISTS _headers)
  # What breaks "compile without the dependency's dev headers" is an #include
  # of one of its headers (or one of its types, which would need such an
  # include). Match #include directives naming curl/wolfssl/wolfcrypt headers;
  # doc-comment prose mentioning the library by name is fine.
  file(STRINGS "${_h}" _inc_hits
       REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]*([Cc][Uu][Rr][Ll]|[Ww][Oo][Ll][Ff][Ss][Ss][Ll]|[Ww][Oo][Ll][Ff][Cc][Rr][Yy][Pp][Tt])")
  # Also reject a leaked type/symbol token: CURL* (uppercase; prose uses
  # lowercase "curl") or a wolfCrypt identifier (wc_*, WOLFSSL*, WC_*,
  # curve25519_*) appearing in a declaration.
  file(STRINGS "${_h}" _curl_hits REGEX "CURL[A-Za-z_]*[ \t]*[*A-Za-z_]")
  file(STRINGS "${_h}" _wolf_hits
       REGEX "(wc_[A-Za-z]|WOLFSSL[A-Za-z_]*|WC_[A-Za-z]|curve25519_)")
  if(_inc_hits OR _curl_hits OR _wolf_hits)
    list(APPEND _bad "${_h}")
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR
    "public headers must stay free of libcurl/wolfSSL but one "
    "includes/declares them: ${_bad}")
endif()

message(STATUS "public headers are libcurl- and wolfSSL-free (${_headers})")
