# toolchain-mingw-w64.cmake — cross-compile Windows x86_64 binaries from Linux
# with the MinGW-w64 toolchain (ARCHITECTURE.md §1: Windows = MinGW/MSYS2).
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
#
# NOTE: a *native* MSYS2/MinGW build does NOT need this file — just configure
# normally inside the "MSYS2 MINGW64" shell (see scripts/install-deps-windows.ps1).
# This file is for cross-building from a Linux host (e.g. package mingw-w64).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_ehem_mingw_prefix x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${_ehem_mingw_prefix}-gcc)
set(CMAKE_CXX_COMPILER ${_ehem_mingw_prefix}-g++)
set(CMAKE_RC_COMPILER  ${_ehem_mingw_prefix}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${_ehem_mingw_prefix})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
