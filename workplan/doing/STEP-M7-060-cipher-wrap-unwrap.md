---
id: STEP-M7-060
title: "ehem_wrap / ehem_unwrap — /api/crypto/cipher/wrap and /unwrap bindings"
milestone: M7
implements: ["REQ-OPS-009"]
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

**Goal:** `ehem_wrap`/`ehem_unwrap` in proto_crypto.c + crypto.h
(direct-KEK and ECDH-KEK flows, optional alg/iv, unwrapped output
zeroized on free), live-verified with the local RFC 3394 cross-check and
the HKDF-info-string arbitration probe.

**Notes:** Before coding, read the firmware's CRYPTO_Wrap/CRYPTO_Unwrap
(crypto.c) for the KEK selection, HKDF info string, and default-IV
ground truth, and the unwrap handler tail (api_crypto.c:1020+) for the
response field name — REQ-OPS-009 has three open criteria hanging on
them. Reuse M6-020's check_peer_args/add_peer_fields and M6-040's
cipher body/error patterns. Local cross-check needs `wc_AesKeyWrap`/
`wc_AesKeyUnWrap` — test-support use of wolfCrypt directly (like the
KAT-style shim tests) or a hidden shim helper; keep it OUT of the
public surface. Probe legs: byte-exact wrap vs local under an
ECDH-derived KEK the test can compute (shim peer); "encedo" vs
"encedo-aes"‖ctx info-string arbitration; msg alignment (8/16/20-byte
msgs); AES256-KEK-serving-AES128 width rule; tamper → 406. EHEMTEST
AES-256 key, cleanup per REQ-TEST-003.

**Definition of done**
- [ ] Both bindings + free functions exported, tagged
      `implements: REQ-OPS-009`; unwrapped buffer zeroized on free;
      header docs record the arbitrated HKDF info string and alignment
      rules
- [ ] Unit tests green (gcc+clang+asan): body bytes for direct/ext_kid/
      pubkey/ctx/iv variants, ARG pre-validation (both-peers, iv≠8, msg
      caps) with zero I/O, 400/403/406 mapping, response decode
- [ ] Live: wrap→unwrap round-trip green; tampered wrapped → 406; local
      wc_AesKeyWrap byte-exact match; REQ-OPS-009 open criteria (info
      string, alignment, width, unwrap field) resolved and recorded
- [ ] Export/header gates green; MinGW cross-syntax check run
