# check_docs_coverage.cmake — REQ-API-007's completeness gate.
#
# verifies: REQ-API-007
#
# The public headers are the per-symbol reference (user decision
# 2026-08-06); docs/API-GUIDE.md must INDEX every public symbol. This
# script extracts the public surface from include/ehem/*.h — EHEM_API
# functions, public typedef names (struct/enum closers and opaque
# forward typedefs), and EHEM_* macros (header guards excluded) — and
# fails listing every symbol the guide does not mention. A new public
# symbol therefore cannot land without documentation.
#
# Usage: cmake -DEHEM_INCLUDE_DIR=<include> -DEHEM_DOCS_FILE=<guide> \
#              -P check_docs_coverage.cmake

if(NOT DEFINED EHEM_INCLUDE_DIR OR NOT DEFINED EHEM_DOCS_FILE)
  message(FATAL_ERROR "pass -DEHEM_INCLUDE_DIR=... and -DEHEM_DOCS_FILE=...")
endif()

file(GLOB _headers "${EHEM_INCLUDE_DIR}/ehem/*.h")
if(_headers STREQUAL "")
  message(FATAL_ERROR "no headers under ${EHEM_INCLUDE_DIR}/ehem")
endif()

set(_symbols "")
foreach(_h IN LISTS _headers)
  file(STRINGS "${_h}" _lines)
  foreach(_l IN LISTS _lines)
    # EHEM_API function declarations (the name right before an open paren).
    if(_l MATCHES "EHEM_API[^(]*[ \\*](ehem_[a-z0-9_]+)\\(")
      list(APPEND _symbols "${CMAKE_MATCH_1}")
    endif()
    # Public typedef names: "} ehem_foo;" struct/enum closers...
    if(_l MATCHES "^} (ehem_[a-z0-9_]+);")
      list(APPEND _symbols "${CMAKE_MATCH_1}")
    endif()
    # ...and opaque forward typedefs: "typedef struct ehem_x ehem_x;".
    if(_l MATCHES "^typedef struct (ehem_[a-z0-9_]+) (ehem_[a-z0-9_]+);")
      list(APPEND _symbols "${CMAKE_MATCH_2}")
    endif()
    # Public macros, header guards excluded.
    if(_l MATCHES "^#[ ]*define (EHEM_[A-Z0-9_]+)")
      set(_m "${CMAKE_MATCH_1}")
      if(NOT _m MATCHES "_H$")
        list(APPEND _symbols "${_m}")
      endif()
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _symbols)
list(LENGTH _symbols _total)

file(READ "${EHEM_DOCS_FILE}" _docs)

set(_missing "")
foreach(_s IN LISTS _symbols)
  string(FIND "${_docs}" "${_s}" _pos)
  if(_pos EQUAL -1)
    list(APPEND _missing "${_s}")
  endif()
endforeach()

if(NOT _missing STREQUAL "")
  list(LENGTH _missing _n)
  string(REPLACE ";" "\n  " _pretty "${_missing}")
  message(FATAL_ERROR
    "docs coverage: ${_n} of ${_total} public symbols are not mentioned "
    "in ${EHEM_DOCS_FILE}:\n  ${_pretty}\n"
    "Index them in the guide (REQ-API-007).")
endif()
message(STATUS "docs coverage: all ${_total} public symbols indexed")
