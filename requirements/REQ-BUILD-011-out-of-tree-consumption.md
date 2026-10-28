---
id: REQ-BUILD-011
title: Out-of-tree consumption — the SDK builds as a CMake subdirectory with its tests and variants switched off, and exports its test support as ehem::test-support
status: approved
priority: should
revision: 1   # 2026-10-28 (M13 decomposition): drafted with the hem-tool split plan of 2026-10-27 and approved with it
source: user decision 2026-10-27 (encedo-hem-tool consumes the SDK as a git submodule for build and tests — the hidden test seams and the fake transport stay reachable through the static link; the release binary links the published package instead); encedo-hem-tool c/CMakeLists.txt and c/tests/CMakeLists.txt at c-v1.3.0 (the consumer shape that exists today: `add_subdirectory(sdk EXCLUDE_FROM_ALL)` with `BUILD_TESTING`, `EHEM_BUILD_TOOL` and `EHEM_BUILD_SHARED` shadowed OFF, and a hand-built `hem_test_support` from `sdk/tests/support/fake_transport.c` with `sdk/src` on its include path); ARCHITECTURE.md §11 M13 part C
depends_on: ["REQ-BUILD-001", "REQ-BUILD-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#10-directory-layout", "ARCHITECTURE.md#11-milestones"]
---

# Out-of-tree consumption

A consumer project SHALL be able to add this repository with
`add_subdirectory(<sdk> [EXCLUDE_FROM_ALL])` after setting, as normal
variables (policy CMP0077 honoured), `BUILD_TESTING=OFF` and
`EHEM_BUILD_SHARED=OFF`, and link `encedo-hem::encedo-hem-static` — no SDK
test, example or install rule is required for that, and the SDK sets a
default `CMAKE_BUILD_TYPE` only when it is the top-level project.

- **`ehem::test-support`.** `tests/support/CMakeLists.txt` SHALL define
  the static library `ehem_test_support` (alias `ehem::test-support`):
  `fake_transport.c`, `ext_sim.c` and the header-only fixtures
  (`integration_env.h`, `ehem_test_keys.h`), with `tests/support` and
  `src` (the hidden seams' declarations) as PUBLIC include directories
  and the static SDK as a PUBLIC dependency; `crypto_ref` (which needs a
  crypto backend header) is included behind the option
  `EHEM_TEST_SUPPORT_CRYPTO_REF` (default ON when `BUILD_TESTING` is on,
  OFF otherwise). The target is built when `EHEM_BUILD_TEST_SUPPORT` is
  ON (default = `BUILD_TESTING`), so a consumer that wants it sets
  `EHEM_BUILD_TEST_SUPPORT ON` before the `add_subdirectory`. The SDK's
  own unit tests link this target (one source list, no duplicate).
- **The seams** reachable this way are the existing hidden ones
  (`ehem_auth_test_set_clock`, `ehem_auth_test_set_kdf_iters`,
  `ehem_ext_test_set_poll_interval`, `ehem_crypto_test_force_mismatch`,
  the fake transport) — nothing new becomes public API.
- **Gate `subdir_consumer`** (CTest, label `unit`): a scratch project under
  `tests/consumer/subdir/` adds the SDK as a subdirectory with the
  variables above, links `encedo-hem::encedo-hem-static` and
  `ehem::test-support`, calls one seam and drives one request through the
  fake transport, and runs; its `ctest -N` lists no SDK test. It runs on
  every CI leg like the package-mode consumer gates.

**Rationale:** encedo-hem-tool's tests reach seams that are not in the
published package (the fast-KDF seam alone turns a 600,000-round PBKDF2
into a unit test), so the tool repository builds the SDK from source for
its tests. Today it rebuilds the test-support sources by hand; a target
exported by this repository keeps the two in step and makes the
subdirectory shape a tested contract rather than an accident.

**Acceptance criteria:**
- [ ] The scratch consumer configures with `BUILD_TESTING=OFF
      EHEM_BUILD_SHARED=OFF` (CMP0077 shadowing): `ctest -N` lists no SDK
      test, no `hem-tool` target exists, the consumer builds and runs
      (STEP-M13-030).
- [ ] `ehem::test-support` exists; the SDK's unit tests link it; the
      `EHEM_TEST_SUPPORT_CRYPTO_REF` and `EHEM_BUILD_TEST_SUPPORT` options
      behave as documented (STEP-M13-030).
- [ ] The `subdir_consumer` gate is green on every ci.yml leg (run id;
      STEP-M13-030).
- [ ] Recorded for the tool repository (not a criterion here): with its pin
      at this SDK version, encedo-hem-tool can replace its hand-built
      `hem_test_support` by `ehem::test-support` and drop
      `EHEM_BUILD_TOOL=OFF`.
