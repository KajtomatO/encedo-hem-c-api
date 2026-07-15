---
id: STEP-M1-080
title: Integration-test gating + first real-device round-trip test
milestone: M1
implements: ["REQ-TEST-002"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M1-060", "STEP-M1-070"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `ctest` with `EHEM_TEST_URL` unset: green, integration tests reported skipped
- [ ] With `EHEM_TEST_URL` set: `ctest -L integration` performs live status+version round-trip successfully
- [ ] Gating implemented once in shared support code
- [ ] Local run instructions documented
