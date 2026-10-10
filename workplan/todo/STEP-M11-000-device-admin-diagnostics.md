---
id: STEP-M11-000
title: "Placeholder — M11: device administration & diagnostics"
milestone: M11
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: "M11 retired 2026-10-07 — provisioning and diag/* parked in BACKLOG.md (user decision); no detailed steps will be created"
---

**Goal:** Milestone M11 per ARCHITECTURE.md §11 (created by user
decision 2026-08-07): `system/config/provisioning` (factory ATECC
certificate write) and the `diag/*` family (9 DIAG-build-only
endpoints). Reserved design decision (user note 2026-08-07): diag
support must NOT enter the production SDK — either hem-tool-only
implementation or a separate diagnostic SDK/library beside the
production one; `libencedo-hem` stays diag-free either way.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and
replaced by detailed steps when M11 is decomposed.

**Definition of done**
- [ ] Never completed as-is — cancelled at M11 decomposition and replaced by detailed STEP-M11-0NN files
