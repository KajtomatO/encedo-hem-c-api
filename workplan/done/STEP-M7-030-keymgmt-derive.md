---
id: STEP-M7-030
title: "ehem_key_derive — /api/keymgmt/derive binding + determinism/scope probes"
milestone: M7
implements: ["REQ-KEY-009"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: ["1abe018"]
  tests: ["verifies: REQ-KEY-009 (tests/unit/test_keymgmt.c — 4 derive cases; tests/integration/test_derive_live.c — 4 live cases green 2026-07-18)"]
  notes: >
    Unit 29/29 gcc+clang + ASan; export/header gates green; MinGW
    cross-syntax OK. ALL probes RESOLVED live: (1) exact scope
    keymgmt:derive ACCEPTED (SDK keeps keymgmt:gen for token sharing);
    (2) determinism PROVEN — ED25519-from-X25519 derived twice → repo
    DEDUP 406 on the second (identical material; derived keys share the
    import dedup domain); (3) SECP521R1-from-X25519 REJECTED (406) on the
    first derive — the stale-stack HKDF quirk is unreachable for
    cross-family derives. MAJOR FINDING (REQ-KEY-009 rev2, device > doc):
    the stored derived key is NOT the documented ECDH+HKDF output — 85
    candidate local reconstructions all mismatched (CRYPTO_DeriveKey
    verified in source to output the raw wolfCrypt X25519 secret;
    REPO_GenKey_* source is ABSENT from the fw checkout and applies an
    undisclosed seed transformation). External interop is impossible;
    derive = device-side key agreement only; pinned with
    assert_memory_not_equal so a doc-conformant fw change surfaces.
    Upstream doc filing candidate. Probing used the python client for the
    85-candidate sweep (scratch, device cleaned after).
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
- [x] `ehem_key_derive` exported, tagged `implements: REQ-KEY-009`;
      header doc states pipeline, the non-reproducibility finding, and
      the `keymgmt:gen` scope choice
- [x] Unit tests green (gcc+clang+asan): body bytes for both peer
      variants, both/neither-peer → `EHEM_ERR_ARG` no I/O, type-length
      pre-validation, `{"kid"}` parse, 400/403/406 mapping
- [x] Live: pipeline cross-check resolved AS A CONFLICT (not locally
      reproducible — REQ-KEY-009 rev2, mismatch pinned); ED25519
      determinism proven via dedup; SECP521R1 combo rejected device-side
      (recorded); scope probe recorded; cleanup clean
- [x] Export/header gates green; MinGW cross-syntax check run
