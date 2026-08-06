---
id: REQ-OPS-001
title: Binding for /api/crypto/exdsa/sign — ECDSA and EdDSA signatures by KID
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M4), §12 risk 3; encedo-hem-api-doc crypto/exdsa-sign.md; encedo_firmware api_crypto.c api_post_crypto_exdsa_sign + crypto.c CRYPTO_Sign (fw v1.2.2); HEM-SDK-7 (sign), HEM-OP-1 (single-part); approved 2026-07-16
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-KEY-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Binding for /api/crypto/exdsa/sign — ECDSA and EdDSA signatures by KID

The SDK SHALL provide `ehem_sign(ctx, kid, alg, msg, msg_len, sig_ctx,
sig_ctx_len, &out)` over `POST /api/crypto/exdsa/sign`, sending
`{kid, msg(base64), alg[, ctx(base64)]}` and returning the decoded `sign`
bytes in a caller-owned struct.

- **Scope — exact, per KID:** the endpoint accepts only the exact scope
  `keymgmt:use:<kid-hex>` (doc and firmware agree: `api_post_crypto_exdsa_sign`
  does a plain `strcmp`, api_crypto.c:456-467 — no `keymgmt:get`/`keymgmt:gen`
  prefix acceptance like `keymgmt/get` has, and `sub == "M"` is rejected).
  This is the same scope string REQ-KEY-003 already requests, so the
  REQ-AUTH-002 cache yields **one token per KID shared by get and sign**.
  This answers the remaining open part of §12 risk 3 from firmware source:
  crypto ops cannot share a broader scope; the per-KID token is mandatory.
  Live confirmation closes the risk (open criterion below).
- **`alg`** is the doc's literal selector, passed verbatim (no client-side
  allowlist, same policy as REQ-KEY-005 `type`): `SHA256WithECDSA`
  (SECP256R1/256K1), `SHA384WithECDSA` (SECP384R1), `SHA512WithECDSA`
  (SECP521R1), `Ed25519`, `Ed25519ph`, `Ed25519ctx`, `Ed448`, `Ed448ph`
  (firmware crypto.c:45-52). The SDK header defines string constants for
  these; unknown strings are the device's 400/406 to report.
- **The device hashes internally** in every variant (`wc_SignatureGenerate`
  with the alg's hash for ECDSA; `wc_ed25519[ph|ctx]_sign_msg` for EdDSA) —
  `msg` is the full message, not a digest, decoded length 1..2048
  (`MAX_API_POST_CRYTO_MSG_LEN`; zero-length is rejected by firmware
  `CRYPTO_Sign`). Consumers signing a pre-hashed digest (PKCS#11 CKM_ECDSA)
  must account for this; the SDK documents it and passes bytes through.
- **`sig_ctx`** (optional, ≤ 255 bytes decoded per RFC 8032) is the EdDSA
  context for `Ed25519ctx`/`Ed25519ph`/`Ed448`/`Ed448ph`; NULL omits the
  field.
- **Signature wire format**, preserved verbatim in the output: ECDSA →
  DER-encoded ECDSA-Sig-Value (variable length), EdDSA → raw RFC 8032
  bytes (Ed25519 64, Ed448 114). Conversion (e.g. DER → fixed r‖s for
  PKCS#11) is the consumer's job, supported by REQ-KEY-006 metadata.

Client-side pre-validation (`EHEM_ERR_ARG`, no network I/O): malformed kid
(not 32 hex chars), NULL/empty msg, msg_len > 2048, sig_ctx_len > 255.
Device errors: 403 → `EHEM_ERR_SCOPE_DENIED`; 400 and 406 →
`EHEM_ERR_DEVICE` with detail retrievable — 406 is deliberately **not**
mapped to `EHEM_ERR_NOT_FOUND` because firmware returns it indistinguishably
for kid-not-found, wrong key type for `alg`, and low-level crypto failure
(`CRYPTO_Sign` ret −2/−3/100 all → 406).

**Rationale:** M4 milestone core (ARCHITECTURE §11) and the PKCS#11
consumer's first signature (HEM-SDK-7, upstream M5). The M4 gate criterion —
a signature produced via the SDK verifies locally with wolfCrypt — lives
here as the live acceptance criterion.

**Acceptance criteria:**
- [x] Request body carries exactly `{kid, msg(b64), alg}` plus `ctx(b64)`
      only when given; `sign` is decoded from padded base64 into a
      caller-owned buffer; `_free` NULL-safe; ASan/LSan clean
      (fake-transport unit tests asserting body bytes). —
      test_sign_body_and_decode / test_sign_with_sig_ctx (byte-exact
      bodies); asan 19/19 clean.
- [x] Declares exact scope `keymgmt:use:<kid>`; after a `ehem_key_get` of
      the same kid, `ehem_sign` performs **no** second token acquisition
      (shared per-KID cache entry — unit test asserting request counts);
      different kids acquire independent tokens. —
      test_sign_shares_get_token (get+sign = 4 requests total; second kid
      → 7 with its own scope decoded from the eJWT).
- [x] Pre-validation failures (`kid` malformed, empty msg, msg > 2048,
      sig_ctx > 255) → `EHEM_ERR_ARG` with zero transport calls; 403 →
      `EHEM_ERR_SCOPE_DENIED`; 400/406 → `EHEM_ERR_DEVICE` (unit tests). —
      test_sign_arg_guards (11 cases, 0 requests), test_sign_device_errors.
- [x] Live (M4 gate): an `EHEMTEST` SECP256R1 key created with mode
      `ExDSA` signs via `SHA256WithECDSA` and the DER signature verifies
      locally with wolfCrypt against the compressed-x963 pubkey from
      `ehem_key_get`; an `EHEMTEST` ED25519 key signs via `Ed25519` and
      the 64-byte signature verifies locally per RFC 8032; cleanup per
      REQ-TEST-003 (integration test). — test_sign_live GREEN on
      my.ence.do fw v1.2.2-DIAG 2026-07-16: ED25519 64-byte sig verified
      (truncated msg correctly rejected); SECP256R1 71-byte DER sig
      verified against the 33-byte COMPRESSED pubkey; classifier sizes
      cross-checked live (pubkey_len 32/33 match REQ-KEY-006).
- [x] ~~OPEN~~ **RESOLVED** (live probe 2026-07-16, closes §12 risk 3):
      sign with a broader-scope token (`keymgmt:get`) → **403 /
      EHEM_ERR_SCOPE_DENIED** exactly as the firmware exact-strcmp
      predicts (the same token class the get endpoint ACCEPTS — the
      asymmetry is confirmed live); get→sign on ONE `keymgmt:use:<kid>`
      token → 200 (test_sign_live scope probe). Crypto ops cannot share a
      broader scope; one cached token per KID serves get+sign. §12 risk 3
      text updated at the M4 gate.
