---
id: STEP-M7-080
title: "M7 gate — live sweep, open-criteria closure, trace regeneration"
milestone: M7
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M7-010", "STEP-M7-020", "STEP-M7-030", "STEP-M7-040", "STEP-M7-050", "STEP-M7-060", "STEP-M7-070"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Fresh `./dev test it` fully green against my.ence.do (pacing
      defaults; device alive throughout)
- [ ] Unit suite green on gcc+clang+ASan; export/header gates green;
      CI green on push (user-confirmed)
- [ ] All M7 REQ open criteria resolved or explicitly carried (each with
      a reason recorded in the REQ)
- [ ] KNOWN-ISSUES.md + upstream filing list updated with M7 findings
- [ ] Device clean per REQ-TEST-003 (0 EHEMTEST, protected keys intact)
- [ ] TRACE.md regenerated (§4.3); REQ statuses transitioned from
      evidence; summary reported in chat
