---
id: STEP-M13-030
title: "tests/support/CMakeLists.txt → ehem::test-support; the subdir_consumer gate; REQ-BUILD-011"
milestone: M13
implements: ["REQ-BUILD-011"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: ["fbc4291", "ef63648"]   # start; the implementation; the done commit (this edit + REQ ticks + TRACE) follows
  tests:
    - "2026-11-03, dev builds build-m13 (gcc, wolfSSL) and build-openssl (gcc, OpenSSL) reconfigured on the new tree: `ctest -L unit` 38/38 on both (37 + subdir_consumer); `ctest -V -R subdir_consumer` reports the scratch build's targets as ehem-cjson, ehem_test_support, encedo-hem-static, subdir_consumer (+ CMake's utility targets) — no test, no program of the SDK's — and `subdir_consumer: OK (encedo-hem 1.2.0; one request through the fake transport, uptime 42)`; the scratch build holds sdk/libencedo-hem.a, sdk/tests/support/libehem_test_support.a and the consumer (0.8 s per run on this host)."
    - "2026-11-03, the options as documented (configure-only runs in the scratchpad, cfg-a…e): (a) BUILD_TESTING on + EHEM_BUILD_TEST_SUPPORT=OFF → configure error 'BUILD_TESTING needs EHEM_BUILD_TEST_SUPPORT=ON (tests/support)'; (b) BUILD_TESTING=OFF → EHEM_BUILD_TEST_SUPPORT defaults OFF, no support, test or probe target; (c) BUILD_TESTING=OFF + EHEM_BUILD_TEST_SUPPORT=ON → EHEM_TEST_SUPPORT_CRYPTO_REF defaults OFF, libehem_test_support.a builds with no crypto_ref object and no test target; (d) (c) + EHEM_BUILD_STATIC=OFF → the support links libencedo-hem-internal.a and builds; (e) the subdir consumer configured without CMAKE_BUILD_TYPE keeps it EMPTY (no Debug default from the SDK), while the top-level configure still defaults to Debug."
    - "2026-11-03, the local matrix on the new tree: `./dev ci` gcc 38/38 + clang 38/38 (wolfSSL), `./dev ci --openssl` gcc 38/38 + clang 38/38, `./dev test asan` 38/38 (the gate's sub-build instrumented through the EHEM_SANITIZE pass-through), fresh shared-only build (EHEM_BUILD_STATIC=OFF — the support and the gate on the private archive) 38/38, fresh static-only build (EHEM_BUILD_SHARED=OFF) 37/37 (no export_symbols without a shared library); the gates (export symbols, public headers, backend-free tests, ascii_strings, docs_coverage, consumer_interface, subdir_consumer) green on every leg. Logs: scratchpad ci-m13-030.log, ci-m13-030-openssl.log, asan-m13-030.log, so-m13-030.log, st-m13-030.log."
    - "2026-11-03, the user's push of ef63648: CI run 41670005625 (2:18 wall) — 10/10 legs green with subdir_consumer on each: Linux gcc 0:29, clang 0:48, gcc shared-only 0:27, gcc static-only 0:31, gcc OpenSSL 0:27, clang OpenSSL 0:33; Windows MinGW 2:06, shared-only 1:36, static-only 2:10, OpenSSL 2:14 (the gate's second SDK build adds seconds, not minutes). The same push's release run 41670085883 ran the gate inside both package steps on both platforms — Release builds against the pinned prefixes, EHEM_DEPS_LINK=static on the OpenSSL one — green."
  notes: "tests/support/CMakeLists.txt defines ehem_test_support + alias ehem::test-support (fake_transport.c, ext_sim.c; crypto_ref_<backend>.c behind EHEM_TEST_SUPPORT_CRYPTO_REF, default BUILD_TESTING; PUBLIC includes src/ and tests/support; PUBLIC link EHEM_STATIC_TARGET). Root CMakeLists.txt: option EHEM_BUILD_TEST_SUPPORT (default BUILD_TESTING, declared after include(CTest)); EHEM_STATIC_TARGET falls back to encedo-hem-internal when BUILD_TESTING OR EHEM_BUILD_TEST_SUPPORT; add_subdirectory(tests/support) before tests/; the Debug default only when CMAKE_SOURCE_DIR == PROJECT_SOURCE_DIR (PROJECT_IS_TOP_LEVEL needs 3.21, the minimum is 3.20). tests/CMakeLists.txt: the inline target replaced by two guards (the SDK's own tests need the support with crypto_ref); the `subdir_consumer` CTest (label unit, native builds only like consumer_interface) runs tests/consumer/check_subdir_consumer.cmake, which configures tests/consumer/subdir (sets BUILD_TESTING OFF, EHEM_BUILD_SHARED OFF, EHEM_BUILD_TEST_SUPPORT ON, EHEM_TEST_SUPPORT_CRYPTO_REF OFF as normal variables; add_subdirectory(<sdk> sdk EXCLUDE_FROM_ALL); links encedo-hem::encedo-hem-static + ehem::test-support) with the parent build's generator, compiler, build type, CMAKE_PREFIX_PATH (`|`-joined, escaped `;`, forward slashes on Windows — check_consumer.cmake's conventions), crypto backend, deps link and sanitizer switch; then `ctest -N` must list no test, CMakeFiles/TargetDirectories.txt no hem-tool / tls-probe / test_* / check_* target, and main.c must print `subdir_consumer: OK` — it calls the hidden seam ehem_auth_test_set_kdf_iters (src/proto_auth.h) and drives ehem_system_status through the fake transport with the unit suite's STATUS_MINIMAL body. The SDK's tests keep linking ehem_test_support by its target name (the alias is the exported spelling). The gate rebuilds the SDK once per CI leg (a few seconds here; the Windows legs will take longer). For encedo-hem-tool (REQ-BUILD-011 criterion 4, recorded at the close): with its pin at this SDK version it replaces its hand-built hem_test_support by `set(EHEM_BUILD_TEST_SUPPORT ON)` + `ehem::test-support` and drops `EHEM_BUILD_TOOL=OFF`. Docs (ARCHITECTURE §10, tests/README.md): STEP-M13-050."
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
- [x] `ehem::test-support` exists with the documented options; the SDK's
      unit and integration tests link it; `./dev ci` green (both compilers,
      both backends). *(2026-11-03: 38/38 on all four legs, ASan,
      shared-only and static-only too.)*
- [x] The scratch consumer: `ctest -N` lists no SDK test, no `hem-tool`
      target; builds and runs (CMP0077 shadowing of `BUILD_TESTING` /
      `EHEM_BUILD_SHARED`). *(2026-11-03, both dev backends; the gate checks
      all three itself.)*
- [x] `subdir_consumer` gate green on all 10 ci.yml legs (run id). *(Run
      41670005625, push of ef63648, 2026-11-03; release run 41670085883 as
      well.)*
- [x] REQ-BUILD-011's criteria ticked (the tool-repository note recorded);
      TRACE regenerated. *(2026-11-03: approved → verified, rev 2.)*
