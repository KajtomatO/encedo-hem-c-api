---
id: STEP-M13-010
title: "Remove hem-tool from the build — sources, CMake targets and options, ./dev tool, the tool tests; disruptive/ unconditional; the shared-only CI leg; REQ-BUILD-001/004/006 re-verified; tags and comments reworded"
milestone: M13
implements: ["REQ-BUILD-001", "REQ-BUILD-004", "REQ-BUILD-006"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#10-directory-layout"]
depends_on: []
evidence:
  commits: ["7394b9e"]   # start; the removal commit follows this edit; the done commit after the CI run
  tests:
    - "2026-10-28, fresh build directory (gcc, wolfSSL, default configure): `cmake -LA -N` lists EHEM_BUILD_SHARED, EHEM_BUILD_STATIC, EHEM_CRYPTO_BACKEND, EHEM_DEPS_LINK, EHEM_SANITIZE — neither EHEM_TOOL_LINK nor EHEM_BUILD_TOOL; no hem-tool target or binary; `ctest -N`: unit 37, integration 21, disruptive 3 (test_storage_live, test_notify_register_live, test_keygen_matrix_live — declared unconditionally now), 61 in all; `ctest -L unit` 37/37; `cmake --install` = include/ehem/*.h (7), lib/libencedo-hem.a, lib/libencedo-hem.so{,.1,.1.2.0}, lib/cmake/encedo-hem/{config,config-version,targets,targets-debug}.cmake, lib/pkgconfig/encedo-hem.pc — the library set only (REQ-BUILD-001/006 criteria)."
    - "2026-10-28: `./dev ci` — gcc 37/37, clang 37/37 (wolfSSL); `./dev ci --openssl` — gcc 37/37, clang 37/37; the gates (export symbols, public headers, ascii_strings over src/+include/, docs_coverage, consumer_interface) green on every leg (logs: scratchpad ci-m13-010.log, ci-m13-010-openssl.log). `shellcheck dev` clean after removing `tool` and `--tool-link`."
    - "2026-10-28: trace generator (summary run): orphan tags none (76 distinct tagged ids, down from 103 — the REQ-TOOL tags left with src/tools and the 19+3 tests); tags still naming superseded REQs: REQ-BUILD-005/009 and REQ-TOOL-026 in scripts/release/*.sh and release.yml only — STEP-M13-020's (package-static.sh, package.sh, build-deps.sh). ci.yml 10 legs on `development`: run id recorded at the close (the user pushes)."
  notes: "Deleted: src/tools/ (39 files, hem-tool + vendored qrcodegen + delayload_win.c), tests/unit: 18 tool tests + tool_force_mismatch.c + check_tool_startup.cmake, tests/integration/test_random_live.c + test_keys_rm_live.c, tests/disruptive/test_cert_install_live.c. CMakeLists.txt: EHEM_TOOL_LINK/EHEM_BUILD_TOOL and their checks, the EHEM_WOLFSSL_DELAYLOAD_LIB block, the hem-tool CLI block gone; EHEM_STATIC_TARGET = encedo-hem-internal whenever BUILD_TESTING and the static variant is off. tests/CMakeLists.txt: ehem_add_disruptive_test (hem-tool-core-bound) removed, add_subdirectory(disruptive) unconditional (plan risk 5 — the three remaining disruptive tests used to vanish with the tool). ./dev: tool, --tool-link, help/completion. ci.yml: the shared-only leg (was 'shared-only, hem-tool on the shared SDK' with EHEM_TOOL_LINK=shared); the switch-leg asserts inspect the installed libencedo-hem.so / .dll instead of build/hem-tool. Comment mentions of REQ-TOOL ids reworded as parentheticals naming encedo-hem-tool (test_cert.c verifies: REQ-SYS-006 only; cert_fixture.h supports: REQ-SYS-006 only). Left for STEP-M13-020 on purpose: scripts/release/package-static.sh (builds hem-tool — the release workflow must not be dispatched between 010 and 020), package.sh's -DEHEM_WOLFSSL_DELAYLOAD_LIB and package-lib.sh's -DEHEM_BUILD_TOOL=OFF (CMake now warns 'manually-specified variables were not used'). Docs (README `./dev tool`, tests/README, KNOWN-ISSUES, ARCHITECTURE §8/§10): STEP-M13-050."
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
- [x] `src/tools/` deleted; `CMakeLists.txt` has no `EHEM_BUILD_TOOL`,
      `EHEM_TOOL_LINK`, delay-load or tool target lines; a default
      configure builds and installs the library set only (install listing
      recorded under REQ-BUILD-001); `cmake -LA` lists neither option
      (REQ-BUILD-006).
- [x] `tests/`: the 19 tool unit tests, `check_tool_startup.cmake` and the
      three tool live tests removed; `add_subdirectory(disruptive)`
      unconditional — `ctest -N -L unit` → 37, `ctest -N -L disruptive` →
      3, `ctest -N -L integration` unchanged minus the two tool tests.
- [x] `./dev`: `tool`, `--tool-link`, their help and completion removed;
      `shellcheck` clean; `./dev ci` green on gcc and clang, wolfSSL and
      OpenSSL (REQ-BUILD-004).
- [ ] `ci.yml`: 10 legs green on `development` (run id) with the renamed
      shared-only leg and the library-inspecting assert steps.
- [ ] `grep -rn hem-tool CMakeLists.txt dev tests/CMakeLists.txt
      tests/unit/CMakeLists.txt` → none; the comment mentions reworded; TRACE
      regenerated — no orphan tag (the REQ-TOOL tags left with the code),
      REQ-BUILD-001/004/006 out of needs-reverify with their new criteria
      ticked.
