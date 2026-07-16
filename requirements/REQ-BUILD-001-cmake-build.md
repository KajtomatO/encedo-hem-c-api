---
id: REQ-BUILD-001
title: CMake build producing static and shared libraries on Linux and Windows (MinGW)
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §1 (CMake ≥3.20; artifact name, user decision 2026-07-15; MinGW/MSYS2, user decision 2026-07-15)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout"]
---

# CMake build producing static and shared libraries on Linux and Windows (MinGW)

The project SHALL build with CMake (≥ 3.20) on Linux (GCC/Clang) and
Windows (MinGW/MSYS2), producing both static and shared variants of the
`encedo-hem` library plus the `hem-tool` executable.

**Rationale:** One cross-platform build system for all targets. The static
variant with hidden internal symbols is the safe path for embedding in a
PKCS#11 module loaded into arbitrary processes; shared is for normal
applications. Artifact naming is fixed: `libencedo-hem.so` /
`libencedo-hem.dll`, CMake target `encedo-hem::encedo-hem` (user decision
2026-07-15). C99, warnings-as-errors baseline.

**Acceptance criteria:**
- [x] `cmake -B build && cmake --build build` succeeds on Linux (GCC and
      Clang) and on Windows under MSYS2/MinGW. *(CI green on both; STEP-M1-020)*
- [x] Both static and shared library targets build from one configure; the
      exported CMake target name is `encedo-hem::encedo-hem`. *(downstream
      `find_package` consumer links it)*
- [x] Sources compile as C99 with `-Wall -Wextra -Werror` (or agreed
      equivalent) clean.
- [x] CTest is wired with labels `unit` and `integration` from the start
      (`disruptive` — renamed from `dangerous`, user decision 2026-07-16 —
      is added when the first such test exists). *(unit runs;
      `integration` label + `ehem_add_integration_test` helper in place)*
- [x] A MinGW toolchain file (or documented MSYS2 invocation) lives in
      `cmake/`. *(cmake/toolchain-mingw-w64.cmake)*
