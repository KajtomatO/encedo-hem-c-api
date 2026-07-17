---
id: STEP-M6-040
title: "ehem_encrypt / ehem_decrypt — /api/crypto/cipher/* bindings"
milestone: M6
implements: ["REQ-OPS-006"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M6-020"]
evidence:
  commits: ["3815983", "a04c6f5"]
  tests: ["verifies: REQ-OPS-006 (tests/unit/test_cipher.c — 5 cases; tests/integration/test_cipher_live.c — 3 cases live green 2026-07-17)"]
  notes: >
    Unit 26/26 gcc+clang + ASan clean; export/header gates green. Live
    (my.ence.do fw v1.2.2-DIAG): GCM ±aad round-trips, flipped tag → 406,
    wrong aad → 406; CBC 40→48 pad/strip; ECB no-iv shape; IV fresh per
    call; width quirk both directions (AES128 alg on AES-256 key OK,
    AES256 alg on AES-128 key → 406). PROBE RESOLVED: derived AES key =
    HKDF-SHA256(secret, info="encedo-aes"‖ctx) — local reproduction matched
    ciphertext+tag byte-for-byte ±ctx; doc's "encedo" does NOT match
    (REQ-OPS-006 rev2). Plaintext zeroized on free; zero-length plaintext
    (external all-padding CBC) tolerated. Post-done fix a04c6f5: %zu in an
    ehem_ctx_fail format broke windows-mingw CI (the known ms_printf
    gotcha) — %d + int cast; cross-syntax re-checked with
    x86_64-w64-mingw32-gcc.
reopened: []
cancelled: null
---

**Goal:** `ehem_encrypt` (→ caller-owned {ciphertext, iv, tag}) and
`ehem_decrypt` (→ zeroized-on-free {plaintext}) in proto_crypto.c +
crypto.h: 9 AES alg literals verbatim, device-generated IV surfaced
verbatim (REQ-OPS-002's harvest source), GCM aad/tag, ECDH-derived flow
(peer args + hkdf_ctx ≤ 64). Live mode matrix + width-quirk probes + the
HKDF-info conflict probe (REQ-OPS-006 open criterion).

**Notes:** Encrypt sends no iv field ever; ECB responses carry no iv —
output struct must distinguish absent from present (len 0 + flag or
documented zero-len). Decrypt iv/tag must be exactly 16 when given
(pre-validate). Response shapes differ by mode — tolerant parse, but
missing ciphertext/plaintext = EHEM_ERR_PROTOCOL. HKDF-info probe:
X25519 pubkey mode, locally HKDF-SHA256 with info "encedo-aes"[+ctx] vs
"encedo", decrypt the device GCM output locally with each candidate
(wolfCrypt directly in the test — tests are not bound by the shim-only
rule) or simpler: encrypt locally and compare ciphertext bytes. Record
the winner in REQ-OPS-006.

**Definition of done**
- [x] `ehem_encrypt` + `ehem_decrypt` (+ output `_free`s, plaintext
      zeroized) exported, tagged `implements: REQ-OPS-006`
- [x] Unit tests green (gcc+clang+asan): body bytes (no iv on encrypt;
      aad/ext_kid/pubkey/ctx only when given; iv/tag on decrypt), ECB
      no-iv response shape, EHEM_ERR_ARG guards (alg length, aad > 16,
      iv/tag ≠ 16, hkdf_ctx > 64, msg bounds) with zero I/O, 400/403/406
      mapping, token sharing
- [x] Live on an EHEMTEST AES-256 key: GCM round-trip ±aad, flipped tag
      bit → 406, wrong aad → 406; CBC round-trips a non-block-aligned
      msg; ECB round-trips 32 bytes with no iv; same msg twice →
      different iv; AES128-GCM on the AES-256 key OK + AES256-GCM on an
      AES-128 key → 406 (width quirk recorded in REQ-OPS-006); cleanup
      per REQ-TEST-003
- [x] HKDF-info probe run; "encedo-aes" vs "encedo" result recorded in
      REQ-OPS-006 (open criterion resolved) and the header doc
- [x] Export + public-header gates green
