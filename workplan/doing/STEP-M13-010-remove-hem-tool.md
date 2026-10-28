---
id: STEP-M13-010
title: "Remove hem-tool from the build — sources, CMake targets and options, ./dev tool, the tool tests; disruptive/ unconditional; the shared-only CI leg; REQ-BUILD-001/004/006 re-verified; tags and comments reworded"
milestone: M13
implements: ["REQ-BUILD-001", "REQ-BUILD-004", "REQ-BUILD-006"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#10-directory-layout"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the repository builds, tests and installs the SDK libraries
only. `src/tools/hem-tool/` is gone with everything that existed for it
(the vendored QR encoder, the Windows delay-load import, the
`EHEM_BUILD_TOOL` / `EHEM_TOOL_LINK` options and their checks, the
`hem-tool-core` / `hem-tool` targets, `./dev tool` and `--tool-link`, the
19 tool unit tests, `check_tool_startup.cmake`, the tool's integration
tests `test_random_live.c` / `test_keys_rm_live.c` and the disruptive
`test_cert_install_live.c`); the three remaining disruptive tests still
run; `ci.yml` keeps its 10 legs with the tool leg renamed; the §6.2
revisions of REQ-BUILD-001/004/006 are closed.

**Notes:** cross-repository precondition met — encedo-hem-tool `c-v1.3.0`
(2026-10-28) carries the moved code and tests (its STEP-T1-020 proved
them there). Deletions per the plan's list (ARCHITECTURE §11 M13 part A):
`CMakeLists.txt` — the option block, the tool checks, the delay-load
block, the tool targets; the "private static build of the SDK objects for
the tests when `EHEM_BUILD_STATIC=OFF`" stays (REQ-BUILD-006). **Pitfall
(plan risk 5):** `tests/CMakeLists.txt` adds `disruptive/` only when the
tool exists — make the `add_subdirectory(disruptive)` unconditional, or
`test_reboot_live` and its siblings vanish silently. `ci.yml`: the leg
"shared-only, hem-tool on the shared SDK" becomes "shared-only"
(`-DEHEM_BUILD_STATIC=OFF`); its assert step inspects the install listing
and `libencedo-hem.so` / `.dll` instead of `build/hem-tool`. Tags and
comments: `tests/unit/test_cert.c` drops `/ REQ-TOOL-003`; the comment
mentions in `include/ehem/keymgmt.h`, `include/ehem/auth.h`,
`include/ehem/system.h`, `src/proto_system.c`, `src/crypto_shim.h`,
`tests/support/ehem_test_keys.h` reworded to "hem-tool (encedo-hem-tool)".
`maybe_source_hem_env` in `./dev` keeps `test it`. MinGW: nothing new
compiles on Windows, but run the CI anyway — the leg rename is a workflow
edit.

**Definition of done**
- [ ] `src/tools/` deleted; `CMakeLists.txt` has no `EHEM_BUILD_TOOL`,
      `EHEM_TOOL_LINK`, delay-load or tool target lines; a default
      configure builds and installs the library set only (install listing
      recorded under REQ-BUILD-001); `cmake -LA` lists neither option
      (REQ-BUILD-006).
- [ ] `tests/`: the 19 tool unit tests, `check_tool_startup.cmake` and the
      three tool live tests removed; `add_subdirectory(disruptive)`
      unconditional — `ctest -N -L unit` → 37, `ctest -N -L disruptive` →
      3, `ctest -N -L integration` unchanged minus the two tool tests.
- [ ] `./dev`: `tool`, `--tool-link`, their help and completion removed;
      `shellcheck` clean; `./dev ci` green on gcc and clang, wolfSSL and
      OpenSSL (REQ-BUILD-004).
- [ ] `ci.yml`: 10 legs green on `development` (run id) with the renamed
      shared-only leg and the library-inspecting assert steps.
- [ ] `grep -rn hem-tool CMakeLists.txt dev tests/CMakeLists.txt
      tests/unit/CMakeLists.txt` → none; the comment mentions reworded; TRACE
      regenerated — no orphan tag (the REQ-TOOL tags left with the code),
      REQ-BUILD-001/004/006 out of needs-reverify with their new criteria
      ticked.
