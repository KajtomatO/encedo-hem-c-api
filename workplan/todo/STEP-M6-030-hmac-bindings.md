---
id: STEP-M6-030
title: "ehem_hmac / ehem_hmac_verify — /api/crypto/hmac/* bindings"
milestone: M6
implements: ["REQ-OPS-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M6-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_hmac` (hash → caller-owned mac) and `ehem_hmac_verify`
(empty-200 → OK) in proto_crypto.c + crypto.h, both key flows (direct
HMAC key with alg optional-and-ignored-by-fw; ECDH-derived with alg
required + peer args from M6-020's helper). Live direct-mode round-trips
plus the HKDF-conflict probe (REQ-OPS-005 open criterion).

**Notes:** Direct mode: fw derives the hash from the stored key type and
ignores request alg (crypto.c:526) — SDK allows alg NULL and documents
the ignore. Derived-mode probe: local shim X25519 keypair vs EHEMTEST
X25519 device key; compare device MAC to local HMAC-SHA256 keyed with the
raw ECDH secret vs HKDF(secret) — record which matches in REQ-OPS-005.
SDK-side rule worth a unit test: derived mode with alg NULL is the fw
fall-through 406, so pre-validate it as EHEM_ERR_ARG (no I/O).

**Definition of done**
- [ ] `ehem_hmac` + `ehem_hmac_verify` (+ mac `_free`) exported, tagged
      `implements: REQ-OPS-005`
- [ ] Unit tests green (gcc+clang+asan): body bytes for direct/derived ×
      hash/verify, mac decode, empty-200 → OK, derived-without-alg →
      EHEM_ERR_ARG, mac > 64 → EHEM_ERR_ARG, 400/403/406 mapping, token
      sharing
- [ ] Live: SHA2-256 HMAC key hash→verify OK, flipped mac bit → 406; one
      SHA3 family key round-trip (mac length matches family); cleanup per
      REQ-TEST-003
- [ ] HKDF-conflict probe run; raw-vs-HKDF result recorded in REQ-OPS-005
      (open criterion resolved) and the header doc
- [ ] Export + public-header gates green
