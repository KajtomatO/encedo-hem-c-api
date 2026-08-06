---
id: STEP-M1-010
title: Build skeleton — CMake targets, CTest/CMocka wiring, export macro
milestone: M1
implements: ["REQ-BUILD-001", "REQ-API-006"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: []
evidence:
  commits: ["f3364a5", "a0d4fdb (export-check .def hardening for MSYS2 binutils)"]
  tests: ["verifies: REQ-BUILD-001", "verifies: REQ-API-006 (tests/unit/test_version.c, tests/unit/check_exports.cmake)"]
  notes: >
    Verified on the Linux dev machine (2026-07-15): GCC 13.3 and Clang 18.1
    both configure/build/`ctest -L unit` green (test_version + export_symbols);
    static (libencedo-hem.a) and shared (libencedo-hem.so) both build; a
    downstream `find_package(encedo-hem)` links `encedo-hem::encedo-hem` and
    runs; `-Wall -Wextra -Werror` clean; `nm -D` shows the .so exports only
    `ehem_version`. Windows/MinGW: cross-compiled the library with
    cmake/toolchain-mingw-w64.cmake — libencedo-hem.dll builds and its export
    table lists only `ehem_version` (objdump branch of check_exports.cmake
    validated against the real .dll). Running the full unit suite ON Windows
    (needs a MinGW cmocka) is deferred to the CI Windows job (STEP-M1-020);
    that is the only open Definition-of-done box. Also vendored the dependency
    installers (scripts/install-deps-{linux.sh,windows.ps1}) as a build chore.
reopened: []
cancelled: null
---

**Goal:** A repository that builds `encedo-hem` static + shared libraries
and runs a first CMocka unit test via `ctest -L unit` on Linux (GCC/Clang)
and Windows (MSYS2/MinGW), with the directory layout from ARCHITECTURE.md
§10 in place.

**Notes:** Establishes: C99 + `-Wall -Wextra -Werror`, hidden default
visibility with an `EHEM_API` export macro (dllexport/dllimport on
Windows), `include/ehem/ehem.h` with `ehem_version()`, CTest labels `unit`
and `integration` wired, CMocka via FetchContent (pinned) or system
package, MinGW toolchain file in `cmake/`. Tag `implements: REQ-BUILD-001`
at the top-level CMakeLists, `REQ-API-006` at the visibility/export macro.

**Definition of done**
- [x] `cmake -B build && cmake --build build && ctest --test-dir build -L unit` green on Linux (GCC and Clang)
- [x] Same green under MSYS2/MinGW on Windows — *CI windows-mingw job green (`ctest -L unit` 2/2) after the export-check `.def` fix landed in a0d4fdb*
- [x] Static and shared variants build; exported CMake target `encedo-hem::encedo-hem`
- [x] C99, warnings-as-errors clean
- [x] Shared library exports only `ehem_*` symbols (scripted `nm`/export-table check)
- [x] `ehem_version()` returns the project version; covered by the first unit test
