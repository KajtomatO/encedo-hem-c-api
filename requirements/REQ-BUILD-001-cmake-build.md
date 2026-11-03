---
id: REQ-BUILD-001
title: CMake build producing static and shared libraries on Linux and Windows (MinGW)
status: verified
priority: must
revision: 3   # 2026-11-03 (STEP-M13-010 done): the M13 criterion ticked — install listing recorded, CI run 40735080526 green on both platforms; needs-reverify → verified (record only, no meaning change). Rev 2 = 2026-10-28 (M13 decomposition, §6.2 approved with the hem-tool split plan 2026-10-27): meaning change — the build no longer produces the hem-tool executable (hem-tool lives in encedo-hem-tool from the SDK's 1.2.1 on)
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
`encedo-hem` library. *(Until the SDK's 1.2.1 the build also produced the
`hem-tool` executable; hem-tool lives in
[encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) since the
split of 2026-10-27 and builds this SDK from a submodule or links its
published package.)*

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
- [x] M13 (rev 2): a default configure defines no `hem-tool` target and
      `cmake --install` lists the library set only (headers, both
      variants, CMake config, `.pc`) — listing recorded here; CI green on
      both platforms after the removal (STEP-M13-010).
      *(2026-10-28, dev host, fresh build directory, gcc, wolfSSL, default
      configure: no `hem-tool` target or binary (`ctest -N` lists 61 tests —
      unit 37, integration 21, disruptive 3 — none of the tool's);
      `cmake --install` = `include/ehem/` (7 headers), `lib/libencedo-hem.a`,
      `lib/libencedo-hem.so`, `.so.1`, `.so.1.2.0`,
      `lib/cmake/encedo-hem/encedo-hem-config.cmake`, `-config-version.cmake`,
      `-targets.cmake`, `-targets-debug.cmake`, `lib/pkgconfig/encedo-hem.pc`
      — 16 files, the library set only. CI run 40735080526 (push of
      7b549db, 2026-10-28): 10/10 legs green on Linux and Windows.)*
