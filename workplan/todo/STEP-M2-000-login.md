---
id: STEP-M2-000
title: "Placeholder — M2: login"
milestone: M2
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: "M2 decomposed 2026-07-16 (§5.3) — replaced by STEP-M2-010..070"
---

**Goal:** Milestone M2 per ARCHITECTURE.md §11: crypto shim (wolfCrypt
X25519/HMAC + vendored Argon2), eJWT encode, `POST /api/auth/token` flow,
token cache with silent refresh, auth error mapping. Gate: authenticated
round-trip against the real device; Argon2 parameters confirmed against
Encedo Manager (closes §12 risk 2).

**Notes:** Rolling-wave placeholder (§5.3 step 5) — cancelled and replaced
by detailed steps when M2 is decomposed; its REQs (AUTH area) are drafted
then. `implements` is empty by design.

**Definition of done**
- [ ] Never completed as-is — cancelled at M2 decomposition and replaced by detailed STEP-M2-0NN files
