---
id: REQ-TEST-002
title: Integration tests gated on EHEM_TEST_URL
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §9; user decision 2026-07-15 (disposable dev device; CI without device stays green)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
---

# Integration tests gated on EHEM_TEST_URL

Integration tests (CTest label `integration`) SHALL be skipped — reported
as skipped, not failed — whenever the `EHEM_TEST_URL` environment variable
is not set.

**Rationale:** CI has no device access yet (user decision 2026-07-15); the
default build must stay green everywhere while the dev machine, where a HEM
is available, runs the full suite by exporting `EHEM_TEST_URL` (and
`EHEM_TEST_PASSPHRASE` once auth lands in M2). The disposable-device policy
and the `EHEMTEST` label prefix govern what integration tests may touch;
those rules bind from M3 when tests start creating keys.

**Acceptance criteria:**
- [ ] With `EHEM_TEST_URL` unset, `ctest` (all labels) exits green with the
      integration tests reported as skipped.
- [ ] With `EHEM_TEST_URL` set, `ctest -L integration` runs the M1
      status/version round-trip against the real device.
- [ ] The gating mechanism is shared infrastructure (one helper), not
      copy-pasted per test.
