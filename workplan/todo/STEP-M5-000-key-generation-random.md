---
id: STEP-M5-000
title: "Placeholder — M5: key generation & random"
milestone: M5
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: "M5 decomposed 2026-07-16 — replaced by STEP-M5-010..040; hardware random moved out of M5 (no endpoint in fw v1.2.2; plan = REQ-OPS-002 encrypt-IV harvest at M6, user decision 2026-07-16)"
---

**Goal:** Milestone M5 per ARCHITECTURE.md §11: `create`/generate for all
key families (EC, EdDSA, X25519/448, AES, HMAC, ML-KEM, ML-DSA); hardware
`random`. Gate: generate → list → sign → delete cycle per family on the
real device.

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and replaced
by detailed steps when M5 is decomposed.

**Definition of done**
- [ ] Never completed as-is — cancelled at M5 decomposition and replaced by detailed STEP-M5-0NN files
