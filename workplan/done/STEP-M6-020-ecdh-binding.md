---
id: STEP-M6-020
title: "ehem_ecdh — /api/crypto/ecdh binding + X25519 local cross-check"
milestone: M6
implements: ["REQ-OPS-004"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: ["b1a82f7"]
  tests: ["verifies: REQ-OPS-004 (tests/unit/test_ecdh.c — 5 cases; tests/integration/test_ecdh_live.c — 3 cases live green 2026-07-17)"]
  notes: >
    Unit 24/24 gcc+clang + ASan clean; export/header gates green. Live
    (my.ence.do fw v1.2.2-DIAG): X25519 pubkey-mode secret byte-exact vs
    shim ehem_x25519_shared; SHA2-256 variant == local hash; SECP256R1
    ext_kid symmetry (A,B)==(B,A); P-256×Curve25519 mismatch → 406. PROBE
    RESOLVED: P-384 raw ecdh = 32 BYTES (fw truncation CONFIRMED live; doc's
    48 wrong; REQ-OPS-004 rev2 records it, test pinned to 32, header doc
    warns to use hashed algs for >256-bit curves; SHA2-384 variant = 48 =
    full-secret digest). check_peer_args/add_peer_fields helpers added for
    M6-030/040 reuse.
reopened: []
cancelled: null
---

**Goal:** `ehem_ecdh(ctx, kid, ext_kid, pubkey, pubkey_len, alg, &out)` in
proto_crypto.c + crypto.h with a zeroized-on-free output struct. Peer =
exactly one of ext_kid / pubkey; alg optional (6 hash literals, verbatim).
Live: X25519 byte-exact cross-check against the crypto shim, ext_kid
symmetry for NIST keys, and the raw-mode truncation probe (REQ-OPS-004
open criterion).

**Notes:** This step establishes the shared peer-argument validation
(exactly-one, pubkey ≤ 67 bytes) that M6-030/M6-040 reuse — keep it a
proto_crypto-internal helper. Peer pubkey formats: NIST = compressed x963
(what ehem_key_get returns — compose in the live test), X25519/448 = raw
LE. Truncation probe: raw ecdh on a P-384 EHEMTEST pair; record 32 vs 48
in REQ-OPS-004 + binding doc. Local X25519 side uses existing shim
primitives (ehem_x25519_keypair_from_seed / ehem_x25519_shared); no shim
changes expected.

**Definition of done**
- [x] `ehem_ecdh` + `ehem_ecdh_secret_free` exported, tagged
      `implements: REQ-OPS-004`; secret buffer zeroized on free
- [x] Unit tests green (gcc+clang+asan): body bytes for ext_kid / pubkey /
      alg variants, both-or-neither peer → EHEM_ERR_ARG no I/O, 403/406
      mapping, token sharing with get
- [x] Live: X25519 pubkey-mode secret == shim-computed secret (byte-exact)
      and SHA2-256 variant == local hash; NIST ext_kid symmetry A/B ==
      B/A; family-mismatch → 406; cleanup per REQ-TEST-003
- [x] Raw-mode truncation probe run on P-384; result recorded in
      REQ-OPS-004 (open criterion resolved) and the header doc
- [x] Export + public-header gates green
