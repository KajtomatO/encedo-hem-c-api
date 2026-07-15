# check_exports.cmake — assert the shared library exports ONLY ehem_* symbols.
#
# verifies: REQ-API-006
#
# Run as a CTest (label `unit`):
#   cmake -DEHEM_SHARED_LIB=<path-to-libencedo-hem.so|.dll> -P check_exports.cmake
#
# Linux/macOS: parse `nm -D --defined-only`.
# Windows (MinGW): parse the export table from `objdump -p`.

cmake_minimum_required(VERSION 3.20)  # policies NEW (CMP0057 IN_LIST) in -P mode

if(NOT DEFINED EHEM_SHARED_LIB)
  message(FATAL_ERROR "EHEM_SHARED_LIB not set")
endif()
if(NOT EXISTS "${EHEM_SHARED_LIB}")
  message(FATAL_ERROR "shared library not found: ${EHEM_SHARED_LIB}")
endif()

# Linker/runtime symbols that legitimately appear in a shared object and are
# not part of our API surface.
set(_allowed
  _init _fini _end _edata __bss_start
  _GLOBAL_OFFSET_TABLE_ __gmon_start__
  _ITM_registerTMCloneTable _ITM_deregisterTMCloneTable
  __cxa_finalize register_frame_info deregister_frame_info)

set(_symbols "")

# Key off the artifact, not the host OS, so the correct tool is used even when
# cross-inspecting (e.g. checking a cross-built .dll from a Linux host).
if(EHEM_SHARED_LIB MATCHES "\\.dll$")
  find_program(_objdump NAMES objdump llvm-objdump x86_64-w64-mingw32-objdump)
  if(NOT _objdump)
    message(WARNING "objdump not found; skipping export check for ${EHEM_SHARED_LIB}")
    return()
  endif()
  execute_process(COMMAND "${_objdump}" -p "${EHEM_SHARED_LIB}"
                  OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "objdump failed on ${EHEM_SHARED_LIB}")
  endif()
  # objdump -p prints many "[  N] token" rows (exports, base relocations, ...).
  # Only the block under "[Ordinal/Name Pointer] Table" holds export names —
  # scan exactly that block so .reloc rows (DIR64/ABSOLUTE) aren't mistaken
  # for exports.
  string(REGEX REPLACE "\r?\n" ";" _lines "${_out}")
  set(_in_names OFF)
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "\\[Ordinal/Name Pointer\\] Table")
      set(_in_names ON)
    elseif(_in_names)
      if(_line MATCHES "^[ \t]*\\[ *[0-9]+\\][ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*$")
        list(APPEND _symbols "${CMAKE_MATCH_1}")
      elseif(_line MATCHES "[^ \t]")   # any other non-blank line ends the table
        set(_in_names OFF)
      endif()
    endif()
  endforeach()
else()
  find_program(_nm NAMES nm llvm-nm)
  if(NOT _nm)
    message(FATAL_ERROR "nm not found; cannot check exported symbols")
  endif()
  execute_process(COMMAND "${_nm}" -D --defined-only "${EHEM_SHARED_LIB}"
                  OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "nm failed on ${EHEM_SHARED_LIB}")
  endif()
  string(REPLACE "\n" ";" _lines "${_out}")
  foreach(_line IN LISTS _lines)
    # "<addr> <type> <name>" — type letter T/W/D/B/R... uppercase == global.
    if(_line MATCHES "^[0-9a-fA-F]+[ \t]+[A-Za-z][ \t]+([A-Za-z_.][A-Za-z0-9_.]*)$")
      list(APPEND _symbols "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endif()

if(NOT _symbols)
  message(FATAL_ERROR "no exported symbols found in ${EHEM_SHARED_LIB} (parser mismatch?)")
endif()
list(REMOVE_DUPLICATES _symbols)

set(_bad "")
foreach(_s IN LISTS _symbols)
  if(_s MATCHES "^ehem_")
    # public API — fine
  elseif(_s IN_LIST _allowed)
    # tolerated linker/runtime symbol
  else()
    list(APPEND _bad "${_s}")
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR
    "shared library exports non-ehem_ symbol(s): ${_bad}\n"
    "all exports were: ${_symbols}")
endif()

if(NOT "ehem_version" IN_LIST _symbols)
  message(FATAL_ERROR "ehem_version is not exported")
endif()

message(STATUS "export check OK — exported: ${_symbols}")
