---
id: STEP-M2-070
title: "M2 gate: live authenticated round-trip; record KDF/sub facts; close §12 risk 2"
milestone: M2
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M2-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore step — verification, no new REQ): against
the real dev-machine HEM, demonstrate login → scoped call → silent
refresh behavior end to end; `ctest -L integration` fully green with
EHEM_TEST_URL + EHEM_TEST_PASSPHRASE; `hem-tool` authenticated paths
work. Record into REQ-AUTH-001's open criterion: the accepted KDF
(PBKDF2-600k confirmed) and the issued token's `sub` claim (U or M).
Update ARCHITECTURE §12 risk 2 to resolved. Resolve REQ-SYS-004's open
criterion (live config shape).

**Notes:** Chore/gate step, `implements: []` per §5.1 — it verifies
REQ-AUTH-001/002/003 + REQ-SYS-004..006 rather than adding scope. The
disruptive cert-install live run is NOT required for the gate (may stay
deferred per REQ-TOOL-003). After the gate: regenerate TRACE.md (§4.3)
once the user has committed the M2 work.

**Definition of done**
- [ ] Live: login succeeds, an authenticated binding returns device data,
      cache reuse observed (request counting via verbose/debug output or
      test assertion).
- [ ] REQ-AUTH-001 + REQ-SYS-004 open criteria resolved with live facts;
      ARCHITECTURE §12 risk 2 marked resolved.
- [ ] All unit + integration suites green on GCC + Clang; ASan/LSan clean;
      CI green.
