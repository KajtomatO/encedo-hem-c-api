---
id: STEP-M8-090
title: M8 gate — trace regeneration, REQ transitions, §12 risk 6 resolution
milestone: M8
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M8-010", "STEP-M8-020", "STEP-M8-030", "STEP-M8-040", "STEP-M8-050", "STEP-M8-060", "STEP-M8-070", "STEP-M8-080"]
evidence:
  commits: ["aa5b60c"]
  tests: []
  notes: "GATE PASSED 2026-08-06. Unit 38/38 gcc+clang+ASan; export/header gates green. Full live sweep: FOUR attempts — three killed by the KNOWN firmware stall (sweeps died at/just-before test_keygen_matrix_live; an isolated run then proved the matrix hard-stalls the device 4/4 even alone on a fresh boot, falsifying the M5-era 'no single trigger') → matrix QUARANTINED into the disruptive label (REQ-TEST-004 rev 3, user decision 2026-08-06) → final matrix-free sweep 23/23 GREEN (337 s), device alive throughout, left clean (3 keys / 3 protected: TLS pair + the resident phone pairing; 0 EHEMTEST). TRACE regenerated: 78 REQs = 73 verified / 5 implemented; all 7 M8 REQs verified; 0 orphans/broken anchors. ARCHITECTURE §11 M8 gate-passed + §12 risk 6 RESOLVED; KNOWN-ISSUES: M8 findings section + the matrix-trigger stall update. CI-on-push: user-confirmed green (incl. windows-mingw) through STEP-M8-070; the later commits (attended-run fixes: proto_ext.c drift recovery + docs/tests) await the next push — flagged for follow-up. Attended-session date corrections applied (implementation 2026-07-22/23, attended+gate 2026-08-05/06)."
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
- [x] Fresh unit (gcc+clang+ASan) and full integration sweep green
      (23/23 with the matrix quarantined to `disruptive` — user decision
      2026-08-06); device clean after (3 keys, all protected)
- [x] TRACE.md regenerated per §4.3; M8 REQs transitioned on evidence;
      no orphan tags / broken anchors / empty-evidence dones
- [x] ARCHITECTURE §11 (M8 gate-passed) + §12 risk 6 RESOLVED text;
      KNOWN-ISSUES entries added (M8 findings + matrix-trigger update)
- [x] CI green on push confirmed by the user (through STEP-M8-070, incl.
      windows-mingw; post-M8-070 commits flagged for the next push)
