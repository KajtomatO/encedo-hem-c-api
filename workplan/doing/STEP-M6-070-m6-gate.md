---
id: STEP-M6-070
title: "M6 gate — live crypto-ops suite, conflict records, trace regen"
milestone: M6
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M6-010", "STEP-M6-020", "STEP-M6-030", "STEP-M6-040", "STEP-M6-050", "STEP-M6-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** M6 closed: fresh `./dev test it` green against the live device
covering every new op (verify, ecdh, hmac, cipher, mlkem, mldsa, random);
all five M6 open criteria resolved with live-recorded values (ECDH raw
truncation, HMAC raw-vs-HKDF key, cipher HKDF info string, decaps alg
field, mldsa-verify failure status); ARCHITECTURE §8 (+`random`
subcommand) and §11 wording touched up; TRACE.md regenerated per §4.3.

**Notes:** Chore step (implements: []) — the milestone gate per the M2-M5
pattern. Firmware findings that turned out to be bugs (mldsa-verify
status, decaps alg) get a KNOWN-ISSUES.md note + upstream-filing
follow-up (non-blocking). Expect REQ-OPS-002/003/004/005/006/007/008 +
TOOL-010 to flip implemented→verified at the regen if all live boxes are
recorded.

**Definition of done**
- [ ] `./dev ci` unit suite green (gcc+clang) + asan clean; fresh
      `./dev test it` integration run green with EHEM_TEST_PACE_MS
      defaulting per REQ-NET-006
- [ ] All five open criteria carry recorded live values in their REQs;
      firmware bugs noted in KNOWN-ISSUES.md
- [ ] ARCHITECTURE §8/§11 updated (random subcommand; M6 wording)
- [ ] TRACE.md regenerated (§4.3); status transitions reported in chat;
      no EHEMTEST leftovers on the device
