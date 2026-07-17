---
id: REQ-OPS-005
title: Bindings for /api/crypto/hmac/hash and /api/crypto/hmac/verify — MAC by KID
status: approved
priority: must
revision: 2
source: ARCHITECTURE.md §6, §11 (M6); encedo-hem-api-doc crypto/hmac-hash.md, crypto/hmac-verify.md; encedo_firmware api_crypto.c api_post_crypto_hmac_hash/_verify + crypto.c CRYPTO_Hash/CRYPTO_HashVerify (fw v1.2.2); HEM-SDK-7 (HMAC), HEM-OP-1; approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-OPS-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/crypto/hmac/hash and /api/crypto/hmac/verify — MAC by KID

The SDK SHALL provide `ehem_hmac(ctx, kid, alg, msg, msg_len, ext_kid,
pubkey, pubkey_len, &out)` over `POST /api/crypto/hmac/hash` (returning the
decoded `mac` bytes in a caller-owned struct) and its mirror
`ehem_hmac_verify(..., mac, mac_len)` over `POST /api/crypto/hmac/verify`
(empty-body 200 → `EHEM_OK`).

- **Two key flows** (firmware CRYPTO_Hash, crypto.c:502-629):
  - **Direct** (`ext_kid`/`pubkey` NULL): `kid` names an HMAC key; **the
    hash is implied by the stored key's type and the request `alg` is
    ignored by the firmware** (crypto.c:526 overwrites alg with the key
    type). The SDK documents this and allows `alg == NULL` in direct mode.
  - **ECDH-derived** (exactly one of `ext_kid`/`pubkey`, same peer rules
    and formats as REQ-OPS-004): `kid` is an ECDH-capable key; the shared
    secret becomes the HMAC key; `alg` is **required** (alg absent →
    firmware falls through to 406).
- **`alg` literals** (passed verbatim): `SHA2-256`, `SHA2-384`,
  `SHA2-512`, `SHA3-256`, `SHA3-384`, `SHA3-512`.
- **Derived-key construction (doc/firmware CONFLICT, device arbitrates):**
  hmac-hash.md says the derived flow runs "ECDH + HKDF"; firmware
  CRYPTO_Hash uses the **raw ECDH shared secret directly** as the HMAC key
  (crypto.c:541-551 — no HKDF, unlike CRYPTO_Encrypt which HKDFs). Open
  criterion below; interop-relevant for anyone reproducing the MAC
  off-device.
- **Scope:** exact `keymgmt:use:<kid>` on the primary kid, sub != M —
  shared per-KID cached token.
- **Limits** (pre-validation → `EHEM_ERR_ARG`, no I/O): kid not 32 hex,
  NULL/empty msg, msg_len > 2048, mac absent/mac_len > 64 (verify),
  pubkey_len > 67, both `ext_kid` and `pubkey` given.
- **Errors:** 403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`;
  406 (MAC mismatch / wrong key type / ECDH failure, indistinguishable) →
  `EHEM_ERR_DEVICE` with http_status/detail retrievable.

**Rationale:** M6 milestone core (ARCHITECTURE §11). HMAC keys became
creatable at M5 (REQ-TEST-004 matrix: `ATT,SHA2-256…` flag-sets); this
REQ makes them usable. The derived flow doubles as the only place the
device's ECDH+MAC composition is observable with a locally-known secret
(pubkey mode + shim X25519), which is what closes the HKDF conflict.

**Acceptance criteria:**
- [ ] Against the fake transport: hash body carries exactly
      `{kid, msg(b64)}` + `alg`/`ext_kid`/`pubkey(b64)` only when given;
      verify body adds `mac(b64)`; `mac` decoded into a caller-owned
      buffer, `_free` NULL-safe; empty-200 verify → `EHEM_OK`; error
      mapping and `EHEM_ERR_ARG` pre-validation with zero transport calls
      (unit tests asserting body bytes).
- [ ] After a get/sign on the same kid, hmac ops perform no second token
      acquisition (unit test asserting request counts).
- [x] Live direct mode (test_hmac_live, my.ence.do fw v1.2.2-DIAG,
      2026-07-17): an `EHEMTEST` SHA2-256 HMAC key — `ehem_hmac` returned a
      32-byte MAC, `ehem_hmac_verify` of it → `EHEM_OK`, one flipped mac
      bit → 406/`EHEM_ERR_DEVICE`; a SHA3-384 key round-tripped with a
      48-byte MAC (key type decides); cleanup clean.
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-17, test_hmac_live):** the
      device MAC in the derived flow byte-matches local **HMAC-SHA256
      keyed with the RAW ECDH secret** — the firmware applies NO HKDF
      (crypto.c:541-559 confirmed on-device); the doc's "ECDH + HKDF"
      claim is wrong (device > doc). Interop rule for off-device
      reproduction: key = raw shared secret. Recorded in the ehem_hmac()
      header doc; the probe asserts byte-equality so a firmware change
      surfaces as a failure.
