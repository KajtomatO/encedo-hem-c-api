---
id: STEP-M4-000
title: "Placeholder — M4: signing & scope tokens"
milestone: M4
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: "M4 decomposed 2026-07-16 (§5.3) — replaced by STEP-M4-010..060"
---

**Goal:** Milestone M4 per ARCHITECTURE.md §11: per-KID scope-token
acquisition and cache (closes §12 risk 3); public-key read; `sign` for
ECDSA and Ed25519; key-type metadata for consumer-side length tables.
Gate: signature produced via the SDK verifies locally with wolfCrypt.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and replaced
by detailed steps (OPS and AUTH area REQs) when M4 is decomposed.

**Definition of done**
- [ ] Never completed as-is — cancelled at M4 decomposition and replaced by detailed STEP-M4-0NN files
