---
id: STEP-M1-080
title: Integration-test gating + first real-device round-trip test
milestone: M1
implements: ["REQ-TEST-002"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M1-060", "STEP-M1-070"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-TEST-002 (tests/integration/test_system_live.c gated via tests/support/integration_env.h)"]
  notes: >
    Shared gating in tests/support/integration_env.h (header-only, public-API
    only — integration tests link the shared lib): ehem_require_test_url()
    exit(77)s when EHEM_TEST_URL is unset, and ehem_test_ctx() builds a device
    context applying EHEM_TEST_INSECURE=1 / EHEM_TEST_CACERT=<file> TLS options.
    ehem_add_integration_test() sets CTest SKIP_RETURN_CODE 77 so an unset URL
    is reported "Skipped", not failed, and adds tests/support to the include
    path. First integration test tests/integration/test_system_live.c performs
    the M1 status+version round-trip (asserts uptime present and hwv/fwv/blv
    non-NULL, prints last_error on failure via fail_msg). Run instructions in
    tests/README.md. Verified on Linux (2026-07-15): with EHEM_TEST_URL UNSET,
    `ctest` (all labels) 100% green with test_system_live reported Skipped;
    with EHEM_TEST_URL set to a local mock HEM, `ctest -L integration` runs the
    round-trip and passes — proving the "URL set → runs" path. The live run
    against the REAL dev-machine HEM is STEP-M1-100 (that step owns the physical
    device and REQ-NET-003/-SYS-001/-002 open criteria).
reopened: []
cancelled: null
---

**Goal:** Shared gating infrastructure that skips `integration`-labeled
tests cleanly when `EHEM_TEST_URL` is unset, plus the first integration
test: status + version round-trip against the real dev-machine HEM.

**Notes:** One helper (e.g. `tests/support/integration_env.h`) reads
`EHEM_TEST_URL` and skips via the CMocka skip mechanism — not copy-pasted
per test. The integration test may need a TLS option depending on the
device certificate model; whatever it needs is input for STEP-M1-100's
REQ-NET-003 verification. Document in tests/README (or CMake comment) how
to run locally.

**Definition of done**
- [x] `ctest` with `EHEM_TEST_URL` unset: green, integration tests reported skipped — *verified: 9/9 green, test_system_live Skipped*
- [x] With `EHEM_TEST_URL` set: `ctest -L integration` performs live status+version round-trip successfully — *path verified against a local mock HEM; live dev-machine run is the M1-100 gate*
- [x] Gating implemented once in shared support code — *tests/support/integration_env.h + ehem_add_integration_test SKIP_RETURN_CODE*
- [x] Local run instructions documented — *tests/README.md*
