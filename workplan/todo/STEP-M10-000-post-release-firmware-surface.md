---
id: STEP-M10-000
title: "Placeholder — M10: device initialisation (auth/init)"
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

**Goal:** Milestone M10 per ARCHITECTURE.md §11 — **`auth/init`** device
personalisation (GET challenge + POST signed init JWT with the 14-field
`cfg` block; initialise a wiped device; moved in from
deliberately-unbound, user decision 2026-08-07). Scope reduced
2026-10-07 by user decision: the `system/upgrade` family and the
`hem-tool fw-upgrade` orchestrator, carried here since 2026-08-06/07,
are parked in BACKLOG.md. Dormant/unrouted surface lives in milestone
MFW (firmware-pending), not here — reassess MFW's list at
decomposition time and pull in anything the then-current firmware
routes.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and replaced
by detailed steps when M10 is decomposed.

**Definition of done**
- [ ] Never completed as-is — cancelled at M10 decomposition and replaced by detailed STEP-M10-0NN files
