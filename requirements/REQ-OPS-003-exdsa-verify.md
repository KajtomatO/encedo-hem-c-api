---
id: REQ-OPS-003
title: Binding for /api/crypto/exdsa/verify — ECDSA and EdDSA signature verification by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M6); encedo-hem-api-doc crypto/exdsa-verify.md; encedo_firmware api_crypto.c api_post_crypto_exdsa_verify + crypto.c CRYPTO_SignVerify (fw v1.2.2); HEM-SDK-7 (verify), HEM-OP-1 (single-part); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-OPS-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Binding for /api/crypto/exdsa/verify — ECDSA and EdDSA signature verification by KID

The SDK SHALL provide `ehem_verify(ctx, kid, alg, msg, msg_len, sig_ctx,
sig_ctx_len, sig, sig_len)` over `POST /api/crypto/exdsa/verify`, sending
`{kid, msg(base64), sign(base64), alg[, ctx(base64)]}` and returning
`EHEM_OK` exactly when the device reports the signature valid (empty-body
200, like reboot/delete/hmac-verify).

- **Scope, alg vocabulary, internal hashing, `sig_ctx`:** identical to
  REQ-OPS-001 (exact `keymgmt:use:<kid>` scope with sub != M — one cached
  per-KID token serves get/sign/verify; the eight `alg` literals passed
  verbatim; the device hashes `msg` internally; `ctx` ≤ 255 bytes decoded,
  omitted when NULL). Firmware: `api_post_crypto_exdsa_verify`
  api_crypto.c:562-717.
- **Limits** (client pre-validation → `EHEM_ERR_ARG`, no I/O): kid not 32
  hex chars, NULL/empty msg, msg_len > 2048 (`MAX_API_POST_CRYTO_MSG_LEN`,
  api.h:252), sig NULL/empty or sig_len > 148 (firmware accepts decoded
  signature ≤ 2·66+16, api_crypto.c:648), sig_ctx_len > 255.
- **Result mapping:** device 406 covers *invalid signature*, *wrong key
  type for alg*, and *kid not found* indistinguishably (`CRYPTO_SignVerify`
  nonzero → 406) → `EHEM_ERR_DEVICE` with http_status/detail retrievable —
  the same deliberate non-mapping as REQ-OPS-001. The binding returns no
  boolean: OK = verified, anything else = not verified with detail.
  403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`.

**Rationale:** M6 milestone core (ARCHITECTURE §11): device-side verify
completes the ExDSA pair from M4. Mirror of the sign binding — doc
(exdsa-verify.md) and firmware agree on shape and scope; no conflicts
found for this endpoint.

**Acceptance criteria:**
- [ ] Against the fake transport: body carries exactly
      `{kid, msg(b64), sign(b64), alg}` plus `ctx(b64)` only when given;
      empty-body 200 → `EHEM_OK`; 406 → `EHEM_ERR_DEVICE`; 403 →
      `EHEM_ERR_SCOPE_DENIED`; pre-validation failures → `EHEM_ERR_ARG`
      with zero transport calls (unit tests asserting body bytes).
- [ ] After `ehem_sign` of the same kid, `ehem_verify` performs no second
      token acquisition (shared per-KID cache entry — unit test asserting
      request counts).
- [ ] Live: an `EHEMTEST` ExDSA key signs via `ehem_sign` and the device
      verifies the signature via `ehem_verify` (`EHEM_OK`); the same
      signature with one flipped bit (and separately a truncated msg) →
      `EHEM_ERR_DEVICE` with http_status 406; cleanup per REQ-TEST-003.
