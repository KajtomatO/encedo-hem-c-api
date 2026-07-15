# check_public_headers.cmake — assert the public headers stay libcurl-free.
#
# verifies: REQ-NET-002 (libcurl never appears in include/ehem/, so a consumer
#           can compile against the SDK without libcurl dev headers).
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
  # What breaks "compile without libcurl headers" is an #include of a curl
  # header (or a CURL type, which would itself need such an include). Match
  # curl #include directives; doc-comment prose mentioning libcurl is fine.
  file(STRINGS "${_h}" _hits
       REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]*[Cc][Uu][Rr][Ll]")
  # Also reject a CURL* / CURLcode type token leaking into a declaration
  # (uppercase CURL; comment prose uses lowercase "curl"/"libcurl").
  file(STRINGS "${_h}" _type_hits REGEX "CURL[A-Za-z_]*[ \t]*[*A-Za-z_]")
  if(_hits OR _type_hits)
    list(APPEND _bad "${_h}")
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR
    "public headers must stay libcurl-free but include/declare curl: ${_bad}")
endif()

message(STATUS "public headers are libcurl-free (${_headers})")
