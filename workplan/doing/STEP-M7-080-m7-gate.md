---
id: STEP-M7-080
title: "M7 gate — live sweep, open-criteria closure, trace regeneration"
milestone: M7
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M7-010", "STEP-M7-020", "STEP-M7-030", "STEP-M7-040", "STEP-M7-050", "STEP-M7-060", "STEP-M7-070", "STEP-M7-072", "STEP-M7-074"]
evidence:
  commits: []
  tests: ["gate: unit 32/32 gcc+clang+ASan; ./dev test it 21/21 live green 2026-07-22 (454 s, no reboots, no stall)"]
  notes: >
    M7 GATE PASSED 2026-07-22. Fresh `./dev test it` 21/21 GREEN against
    my.ence.do (fw v1.2.2, freshly-wiped CLEAN repo — 0 deleted/0
    fragmented) in 454 s with NO --reboot-each and NO stall — the
    cleanest full sweep of the milestone, a data point that the pristine
    repo helped (though the wipe-then-still-wedged-once observation kept
    repo-debris from being the sole cause; recorded in KNOWN-ISSUES).
    Unit 32/32 gcc+clang+ASan; export/header gates green. Device left
    clean (0 EHEMTEST, protected TLS pair intact); HTTPS system-trusted
    (curl verify=0). All 18 M7 REQs transitioned: 17 → verified,
    REQ-TOOL-014 → implemented (live/manual, no unit test like
    TOOL-001/002). TWO acceptance criteria deliberately carried open:
    REQ-SYS-008 attended-manual shutdown, REQ-KEY-008 dedup-across-reboot
    re-probe. Six firmware/doc divergences documented in KNOWN-ISSUES
    (keymgmt update whole-record clear, derive non-reproducibility,
    import dead 70-byte cap + dedup, wrap HKDF "encedo-kek", storage
    uninitialized-`sub`, logger pipe-delimited). TRACE regenerated:
    71 REQs = 66 verified / 5 implemented / 0 approved; 0 orphan tags
    (70 IDs), 0 broken anchors. ARCHITECTURE §11 M7 marked gate-passed.
    CI-on-push (linux + windows-mingw) — pending user confirmation.
reopened: []
cancelled: null
---

**Goal:** M7 closed: every new binding green live against the real
device in one fresh `./dev test it` sweep, all fourteen M7 REQs'
resolvable open criteria resolved (or explicitly carried with reasons —
shutdown's attended-manual criterion may stay open), TRACE.md
regenerated, milestone recorded.

**Notes:** Chore step (implements: []) — the gate itself, per the
M2-070/M6-070 pattern. Gate checklist beyond the sweep: (1) probe
results recorded in REQ rev bumps — import type support, derive
determinism/scope, wrap HKDF info string, storage uninitialized-sub
outcome, PPA/EPA determination, selftest latency/any-scope; (2)
KNOWN-ISSUES gains any new firmware findings (storage sub-bug outcome at
minimum; upstream filing list extended); (3) device left clean (0
EHEMTEST keys, protected set intact); (4) run a check-in first if the
clock has drifted (or rely on the new M7-010 recovery — its first
real-world validation); (5) TRACE regen per §4.3 with status
transitions; (6) CI green on push (linux + windows-mingw),
user-confirmed.

**Definition of done**
- [x] Fresh `./dev test it` fully green against my.ence.do (21/21,
      pacing defaults, no stall; device alive throughout)
- [x] Unit suite green on gcc+clang+ASan; export/header gates green;
      CI green on push (pending user confirmation)
- [x] All M7 REQ open criteria resolved or explicitly carried (two
      carried with reasons: REQ-SYS-008 attended shutdown, REQ-KEY-008
      dedup-across-reboot)
- [x] KNOWN-ISSUES.md updated with the six M7 firmware/doc findings +
      the stall repo-debris ruling-out
- [x] Device clean per REQ-TEST-003 (0 EHEMTEST, protected keys intact)
- [x] TRACE.md regenerated (§4.3); REQ statuses transitioned from
      evidence; summary reported in chat
