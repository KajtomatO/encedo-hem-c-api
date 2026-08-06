---
id: STEP-MFW-000
title: "Placeholder — MFW: firmware-pending surface"
milestone: MFW
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

**Goal:** Milestone MFW per ARCHITECTURE.md §11 (created by user
decision 2026-08-07): the parking bucket for every feature that is
documented — or shipped dormant — but MISSING from the running
firmware, so the SDK cannot build or verify it: logger DELETE,
`POST /api/keymgmt/list` "extended", `system/health`, `stream/*`,
`x509/*` (the undocumented device-side CA), `misc/rnd`. Unscheduled by
design: re-sweep this list on EVERY firmware upgrade; when upstream
routes an item, promote it into a numbered milestone and decompose
there.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — never decomposed
wholesale; items leave one by one as firmware ships them.

**Definition of done**
- [ ] Never completed as-is — items promote to numbered milestones as firmware routes them; cancelled only if the list empties
