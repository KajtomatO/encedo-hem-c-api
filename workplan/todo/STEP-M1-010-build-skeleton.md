---
id: STEP-M1-010
title: Build skeleton — CMake targets, CTest/CMocka wiring, export macro
milestone: M1
implements: ["REQ-BUILD-001", "REQ-API-006"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `cmake -B build && cmake --build build && ctest --test-dir build -L unit` green on Linux (GCC and Clang)
- [ ] Same green under MSYS2/MinGW on Windows
- [ ] Static and shared variants build; exported CMake target `encedo-hem::encedo-hem`
- [ ] C99, warnings-as-errors clean
- [ ] Shared library exports only `ehem_*` symbols (scripted `nm`/export-table check)
- [ ] `ehem_version()` returns the project version; covered by the first unit test
