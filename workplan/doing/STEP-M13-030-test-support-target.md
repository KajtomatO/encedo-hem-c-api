---
id: STEP-M13-030
title: "tests/support/CMakeLists.txt → ehem::test-support; the subdir_consumer gate; REQ-BUILD-011"
milestone: M13
implements: ["REQ-BUILD-011"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the SDK is a tested out-of-tree dependency: a consumer adds it as
a CMake subdirectory with tests and the shared variant off, links the
static target and `ehem::test-support`, reaches the hidden seams and the
fake transport, and a CTest gate proves it on every CI leg.

**Notes:** `tests/support/CMakeLists.txt` defines `ehem_test_support`
(alias `ehem::test-support`): `fake_transport.c`, `ext_sim.c`, PUBLIC
include dirs `tests/support` and `src`, PUBLIC link to the static SDK
(`encedo-hem-static`, or the private static build when
`EHEM_BUILD_STATIC=OFF`); `crypto_ref` behind
`EHEM_TEST_SUPPORT_CRYPTO_REF` (default `BUILD_TESTING`); built when
`EHEM_BUILD_TEST_SUPPORT` (default `BUILD_TESTING`). The SDK's own
`tests/unit` and `tests/integration` link the target instead of listing
the support sources. Top-level `CMakeLists.txt`: set the default
`CMAKE_BUILD_TYPE` only when `PROJECT_IS_TOP_LEVEL`. Gate:
`tests/consumer/subdir/` scratch project (one `CMakeLists.txt` + `main.c`
calling `ehem_auth_test_set_kdf_iters` and one fake-transport request),
driven like `consumer_cmake_static` (configure + build + run in a CTest).
Shape reference: encedo-hem-tool `c/CMakeLists.txt` at `c-v1.3.0`.

**Definition of done**
- [ ] `ehem::test-support` exists with the documented options; the SDK's
      unit and integration tests link it; `./dev ci` green (both compilers,
      both backends).
- [ ] The scratch consumer: `ctest -N` lists no SDK test, no `hem-tool`
      target; builds and runs (CMP0077 shadowing of `BUILD_TESTING` /
      `EHEM_BUILD_SHARED`).
- [ ] `subdir_consumer` gate green on all 10 ci.yml legs (run id).
- [ ] REQ-BUILD-011's criteria ticked (the tool-repository note recorded);
      TRACE regenerated.
