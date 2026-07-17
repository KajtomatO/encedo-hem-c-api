---
id: STEP-M7-030
title: "ehem_key_derive — /api/keymgmt/derive binding + determinism/scope probes"
milestone: M7
implements: ["REQ-KEY-009"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_key_derive` in proto_keymgmt.c + keymgmt.h (peer =
exactly one of ext_kid/pubkey, 17 non-PQC types, scope `keymgmt:gen`),
live-verified with the byte-exact ECDH+HKDF pipeline cross-check and
the REQ-KEY-009 determinism + scope probes.

**Notes:** Reuse the proto_crypto peer-argument validation pattern
(M6-020's check_peer_args semantics — but this binding lives in
proto_keymgmt; extract or mirror, don't fork subtly). The byte-exact
leg needs local HKDF-SHA256 (salt=∅, one expand round for L ≤ 32):
implementable in the test with two `ehem_hmac_sha256` shim calls —
no shim extension required. Cross-check flow: create device X25519
(mode ECDH) → derive SHA2-256 with a shim-known peer pubkey → local
HKDF(shim-ECDH secret, "encedo-sha256") → `ehem_hmac` (M6-030) on the
derived KID vs local HMAC. Determinism probes per REQ-KEY-009 (ED25519
twice = equal pubkeys; SECP521R1-from-X25519 twice = record equal or
not). Scope probe: raw request with an exact `keymgmt:derive` token →
record 200/403. Keys pile up fast here — delete each derived key
promptly (≤ 1 EHEMTEST alive, M5 matrix discipline).

**Definition of done**
- [ ] `ehem_key_derive` exported, tagged `implements: REQ-KEY-009`;
      header doc states pipeline, safe/unsafe determinism combos (from
      the probe), and the `keymgmt:gen` scope choice
- [ ] Unit tests green (gcc+clang+asan): body bytes for both peer
      variants, both/neither-peer → `EHEM_ERR_ARG` no I/O, type-length
      pre-validation, `{"kid"}` parse, 400/403/406 mapping
- [ ] Live: byte-exact HKDF pipeline proof green; ED25519 determinism
      green; SECP521R1 nondeterminism probe recorded in REQ-KEY-009;
      scope probe recorded; cleanup clean
- [ ] Export/header gates green; MinGW cross-syntax check run
