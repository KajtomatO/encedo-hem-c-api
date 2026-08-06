---
id: STEP-M6-070
title: "M6 gate — live crypto-ops suite, conflict records, trace regen"
milestone: M6
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M6-010", "STEP-M6-020", "STEP-M6-030", "STEP-M6-040", "STEP-M6-050", "STEP-M6-060"]
evidence:
  commits: ["aa6c19f"]
  tests: ["full `./dev test it` 17/17 GREEN 2026-07-17 (283 s, device stable; check-in clock resync first)", "unit 29/29 gcc+clang + ASan clean", "CI green linux + windows-mingw (user-confirmed)"]
  notes: >
    M6 COMPLETE. All five doc/fw conflict probes were already resolved in
    their implementing steps (REQ-OPS-004/005/006/007/008 rev2s) — nothing
    carried into the gate. Gate deliverables: 8 REQs approved→verified
    (OPS-002..008, TOOL-010); TRACE.md regenerated (53 REQs = 49 verified /
    4 implemented / 0 approved / 0 draft; no orphans, no broken anchors);
    KNOWN-ISSUES.md gained the PQC response-bug entry (decaps alg
    scratch-buffer echo; mldsa-verify raw status 795) — the clock-drift
    entry (+ session-start check-in recommendation) landed pre-gate as
    e20119e. ARCHITECTURE §8 (+random) and §11 (M6 notes+gate) were
    amended at decomposition — re-verified, no further edit needed.
    Device clean post-suite: 0 EHEMTEST keys (4 device keys, 3 protected).
    Upstream firmware filings (non-blocking follow-ups): RTC ~8% fast;
    exp clamping; decaps alg_used param; mldsa-verify 406 mapping;
    watchdog/hooks from the M5 stall entry.
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
- [x] `./dev ci` unit suite green (gcc+clang) + asan clean; fresh
      `./dev test it` integration run green with EHEM_TEST_PACE_MS
      defaulting per REQ-NET-006 (17/17, 283 s)
- [x] All five open criteria carry recorded live values in their REQs;
      firmware bugs noted in KNOWN-ISSUES.md
- [x] ARCHITECTURE §8/§11 updated (random subcommand; M6 wording — done
      at decomposition, re-verified at the gate)
- [x] TRACE.md regenerated (§4.3); status transitions reported in chat;
      no EHEMTEST leftovers on the device (0 found post-suite)
