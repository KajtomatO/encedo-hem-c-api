---
id: REQ-OPS-006
title: Bindings for /api/crypto/cipher/encrypt and /api/crypto/cipher/decrypt — AES by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M6: GCM IV/tag handling); encedo-hem-api-doc crypto/cipher-encrypt.md, crypto/cipher-decrypt.md, FIRMWARE_NOTES.md:49; encedo_firmware api_crypto.c api_post_crypto_cipher_encrypt/_decrypt + crypto.c CRYPTO_Encrypt/CRYPTO_Decrypt (fw v1.2.2); HEM-SDK-7 (AES), HEM-OP-1; approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-OPS-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/crypto/cipher/encrypt and /api/crypto/cipher/decrypt — AES by KID

The SDK SHALL provide `ehem_encrypt(ctx, kid, alg, msg, msg_len, aad,
aad_len, ext_kid, pubkey, pubkey_len, hkdf_ctx, hkdf_ctx_len, &out)` over
`POST /api/crypto/cipher/encrypt` — returning caller-owned `{ciphertext;
iv (CBC/GCM, 16 bytes); tag (GCM, 16 bytes)}` — and its mirror
`ehem_decrypt(..., iv, tag, ..., &out)` over `POST /api/crypto/cipher/
decrypt` returning `{plaintext}` (zeroized on free).

- **`alg` is required** (device 400 unless exactly 10 chars): `AES128-ECB`,
  `AES192-ECB`, `AES256-ECB`, `AES128-CBC`, `AES192-CBC`, `AES256-CBC`,
  `AES128-GCM`, `AES192-GCM`, `AES256-GCM` — passed verbatim (the parser
  comment's "default AES256-GCM" is unreachable; doc Notes agree).
- **IV is always device-generated** (encrypt accepts no `iv` field;
  firmware my_rng_gen_block, api_crypto.c:1385 — the hardware-RNG source
  REQ-OPS-002 harvests). Echoed back for CBC/GCM; **absent for ECB**.
- **Two key flows** (as REQ-OPS-005, firmware CRYPTO_Encrypt):
  - **Direct:** `kid` names an AES key; the requested width must be ≤ the
    stored key width (an AES-256 key serves AES128-* by truncation,
    crypto.c:1304-1321; AES-128 key with AES256-* → 406).
  - **ECDH-derived** (exactly one of `ext_kid`/`pubkey`; optional
    `hkdf_ctx` ≤ 64 bytes): AES key = HKDF-SHA256(ECDH secret,
    info = `"encedo-aes"` + ctx bytes). **Doc/firmware CONFLICT:**
    cipher-encrypt.md says the HKDF info default is `"encedo"`; firmware
    crypto.c:58 pins the prefix `"encedo-aes"` (ctx appends, never
    replaces). Device arbitrates — open criterion below.
- **Mode mechanics** (device-side, SDK documents them): ECB — msg must be
  a 16-multiple (else 400), no integrity; CBC — PKCS#7 added/stripped by
  the device, any msg length; GCM — no padding, `aad` optional ≤ 16 bytes
  decoded, tag mismatch on decrypt → 406.
- **Limits** (pre-validation → `EHEM_ERR_ARG`, no I/O): kid not 32 hex;
  NULL/empty msg; msg_len > 2048 on encrypt (> 2064 on decrypt — firmware
  allows +AES_BLOCK for CBC padding growth); unknown alg length (the SDK
  checks only length 10, literals stay device-validated); aad_len > 16;
  iv/tag given but not exactly 16 (decrypt); hkdf_ctx_len > 64;
  pubkey_len > 67; both `ext_kid` and `pubkey`.
- **Scope:** exact `keymgmt:use:<kid>`, sub != M, shared per-KID token.
  **Errors:** 403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`;
  406 (key/width mismatch, tag mismatch, ECDH failure) →
  `EHEM_ERR_DEVICE` with detail.

**Rationale:** M6 milestone core — "AES encrypt/decrypt (GCM IV/tag
handling)" (ARCHITECTURE §11). AES keys became creatable at M5. Also the
substrate for REQ-OPS-002 (`ehem_random` harvests encrypt's IV), which is
why encrypt must expose the returned IV verbatim.

**Acceptance criteria:**
- [ ] Against the fake transport: encrypt body carries exactly
      `{kid, msg(b64), alg}` + `aad`/`ext_kid`/`pubkey`/`ctx` only when
      given (no `iv` field ever); decrypt body adds `iv`/`tag` when given;
      response `ciphertext`/`iv`/`tag`/`plaintext` decoded into
      caller-owned buffers (iv/tag absent → zeroed/flagged, ECB shape);
      plaintext zeroized on free; error mapping and `EHEM_ERR_ARG`
      pre-validation with zero transport calls (unit tests asserting body
      bytes).
- [ ] Live GCM round-trip on an `EHEMTEST` AES-256 key: encrypt (with and
      without aad) → 16-byte iv + 16-byte tag + msg-length ciphertext →
      decrypt returns the original plaintext; decrypt with one flipped tag
      bit (and separately wrong aad) → 406/`EHEM_ERR_DEVICE`.
- [ ] Live CBC/ECB on the same key: CBC round-trips a non-block-aligned
      msg (device pads/strips PKCS#7); ECB round-trips a 32-byte msg and
      returns no iv; two encrypts of the same msg → different iv (feeds
      REQ-OPS-002); AES128-GCM with the AES-256 key succeeds
      (width-truncation quirk recorded), AES256-GCM with an `EHEMTEST`
      AES-128 key → 406; cleanup per REQ-TEST-003.
- [ ] OPEN (live probe, closes the HKDF-info conflict): derived mode via
      `pubkey` with a local shim X25519 keypair — locally compute
      HKDF-SHA256(ECDH secret, info=`"encedo-aes"`[+ctx]) vs
      info=`"encedo"` and decrypt the device's GCM output with each
      candidate key; record which matches here and in the binding doc.
