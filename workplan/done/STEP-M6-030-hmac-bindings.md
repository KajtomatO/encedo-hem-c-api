---
id: STEP-M6-030
title: "ehem_hmac / ehem_hmac_verify — /api/crypto/hmac/* bindings"
milestone: M6
implements: ["REQ-OPS-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M6-020"]
evidence:
  commits: ["495162c"]
  tests: ["verifies: REQ-OPS-005 (tests/unit/test_hmac.c — 5 cases; tests/integration/test_hmac_live.c — 3 cases live green 2026-07-17)"]
  notes: >
    Unit 25/25 gcc+clang + ASan clean; export/header gates green. Live
    (my.ence.do fw v1.2.2-DIAG): SHA2-256 key hash→verify OK, flipped mac →
    406; SHA3-384 key → 48-byte MAC (key type decides, request alg ignored
    in direct mode). PROBE RESOLVED: derived-mode HMAC key = RAW ECDH secret
    — device MAC byte-matched local HMAC-SHA256(raw X25519 secret); NO HKDF
    (doc wrong; REQ-OPS-005 rev2 records the interop rule). SDK
    pre-validates derived-without-alg as EHEM_ERR_ARG (fw would 406
    opaquely).
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
- [x] `ehem_hmac` + `ehem_hmac_verify` (+ mac `_free`) exported, tagged
      `implements: REQ-OPS-005`
- [x] Unit tests green (gcc+clang+asan): body bytes for direct/derived ×
      hash/verify, mac decode, empty-200 → OK, derived-without-alg →
      EHEM_ERR_ARG, mac > 64 → EHEM_ERR_ARG, 400/403/406 mapping, token
      sharing
- [x] Live: SHA2-256 HMAC key hash→verify OK, flipped mac bit → 406; one
      SHA3 family key round-trip (mac length matches family); cleanup per
      REQ-TEST-003
- [x] HKDF-conflict probe run; raw-vs-HKDF result recorded in REQ-OPS-005
      (open criterion resolved) and the header doc
- [x] Export + public-header gates green
