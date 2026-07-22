---
id: STEP-M8-090
title: M8 gate — trace regeneration, REQ transitions, §12 risk 6 resolution
milestone: M8
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M8-010", "STEP-M8-020", "STEP-M8-030", "STEP-M8-040", "STEP-M8-050", "STEP-M8-060", "STEP-M8-070", "STEP-M8-080"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** milestone closure: fresh full `./dev ci` + `./dev test it`
sweep green (checkin clock-resync first per the standing RTC-drift
ritual), device left clean (no EHEMTEST/EXTAID test residue), all M8
REQ status transitions applied during §4.3 trace regeneration,
ARCHITECTURE §11 M8 marked gate-passed and §12 risk 6 marked RESOLVED
with the doc-analysis + live findings, KNOWN-ISSUES updated (dead
anti-bruteforce delay; tester-`exp` divergence; any broker surprises
from M8-080).

**Notes:** chore, `implements: []` — the gate is process, not product
(M2-070/M7-080 precedent). Upstream filing candidates accumulated in
M8 join the standing list. Check CI on push (linux + windows-mingw)
before declaring the milestone complete.

**Definition of done**
- [ ] Fresh unit (gcc+clang+ASan) and full integration sweep green;
      device clean after
- [ ] TRACE.md regenerated per §4.3; M8 REQs transitioned on evidence;
      no orphan tags / broken anchors / empty-evidence dones
- [ ] ARCHITECTURE §11 (M8 gate-passed) + §12 risk 6 RESOLVED text;
      KNOWN-ISSUES entries added
- [ ] CI green on push confirmed by the user
