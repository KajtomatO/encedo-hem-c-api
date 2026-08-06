---
id: STEP-M10-000
title: "Placeholder — M10: 1.0+ post-release firmware surface"
milestone: M10
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Milestone M10 per ARCHITECTURE.md §11 (split out of the 1.0
release, user decision 2026-08-06; scope extended 2026-08-07): the
`system/upgrade` family deferred since M7 (fw upload/check/install
triad, ui triad, bootloader upload, usbmode + the live auth-gated base
`GET /api/system/upgrade`) plus the `hem-tool fw-upgrade` orchestrator;
**`auth/init`** device personalisation (moved in from
deliberately-unbound, user decision 2026-08-07); the dormant-route
re-check — `stream/*` (4) and the undocumented device-side CA `x509/*`
(7) are commented out of the v1.2.2 route table with handlers shipped;
reassess against the firmware current at decomposition time.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and replaced
by detailed steps when M10 is decomposed.

**Definition of done**
- [ ] Never completed as-is — cancelled at M10 decomposition and replaced by detailed STEP-M10-0NN files
