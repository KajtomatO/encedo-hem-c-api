---
id: REQ-OPS-008
title: Bindings for /api/crypto/pqc/mldsa/sign and /verify — ML-DSA by KID
status: verified
priority: must
revision: 2
source: ARCHITECTURE.md §6, §11 (M6); encedo-hem-api-doc crypto/pqc/mldsa-sign.md, mldsa-verify.md; encedo_firmware api_crypto.c api_post_crypto_pqc_mldsa_sign/_verify + crypto.c CRYPTO_MLDSA_Sign/Verify (fw v1.2.2); HEM-SDK-7 (sign), HEM-OP-1; approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-OPS-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/crypto/pqc/mldsa/sign and /verify — ML-DSA by KID

The SDK SHALL provide `ehem_mldsa_sign(ctx, kid, msg, msg_len, sig_ctx,
sig_ctx_len, &out)` over `POST /api/crypto/pqc/mldsa/sign` — returning
caller-owned `{alg string; sig}` — and `ehem_mldsa_verify(..., sig,
sig_len)` over `POST /api/crypto/pqc/mldsa/verify` (empty-body 200 →
`EHEM_OK`).

- **Request shapes** (firmware api_crypto.c:2231-2553): sign
  `{kid, msg(base64)[, ctx(base64)]}`; verify adds `sign(base64)`. The
  parameter set is fixed by the key; sign's response `alg` reports
  `"MLDSA44"|"MLDSA65"|"MLDSA87"`; signature sizes 2420/3309/4627
  (`DILITHIUM_ML_DSA_{44,65,87}_SIG_SIZE`). `ctx` is the FIPS 204 signing
  context, ≤ 255 bytes decoded, omitted when NULL — a signature made with
  ctx verifies only with the same ctx (wc_dilithium_*_ctx_msg paths).
- **Limits** (pre-validation → `EHEM_ERR_ARG`, no I/O): kid not 32 hex,
  NULL/empty msg, msg_len > 2048, sig_ctx_len > 255, sig NULL/empty or
  sig_len > 4627 (verify).
- **Verify failure status (doc/firmware CONFLICT, device arbitrates):**
  mldsa-verify.md documents 406 for an invalid signature, but the fw
  v1.2.2 handler has no 406 mapping — it passes `CRYPTO_MLDSA_Verify`'s
  raw failure code (e.g. wolfSSL `SIG_VERIFY_E` = −229, or −2/−3) straight
  into the HTTP status line, which httpio.c:1223 prints as an unsigned
  decimal (a garbage status such as 65307). The SDK therefore maps **any
  non-200 completion** of a verify request to `EHEM_ERR_DEVICE` (403
  excepted) with the raw status retrievable, and must tolerate a status
  outside 100-599 without treating it as a transport/protocol failure.
  Open criterion records the live value.
- **Scope:** exact `keymgmt:use:<kid>`, sub != M, shared per-KID token.
  **Errors:** 403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`;
  sign 406 (not ML-DSA / not found / crypto failure) → `EHEM_ERR_DEVICE`
  with detail.

**Rationale:** completes the M6 "remaining ML-DSA parameter sets"
milestone item (ARCHITECTURE §11) — M4's REQ-OPS-001 covered only the
`exdsa` endpoint (ECDSA/EdDSA); ML-DSA signs via its own `pqc` endpoint
with a different response shape (`alg` + raw `sign`) and no client-side
`alg` selector. ML-DSA keys became creatable at M5 (`ATT,PKEY,PQC,MLDSA…`).

**Acceptance criteria:**
- [ ] Against the fake transport: sign body carries exactly
      `{kid, msg(b64)}` + `ctx(b64)` only when given; verify adds
      `sign(b64)`; `sig` decoded to a caller-owned buffer, `_free`
      NULL-safe; a verify response with an out-of-range status (e.g.
      65307) → `EHEM_ERR_DEVICE`, not `EHEM_ERR_PROTOCOL`/crash; error
      mapping and `EHEM_ERR_ARG` pre-validation with zero transport calls
      (unit tests).
- [x] Live round-trip (test_pqc_live, my.ence.do fw v1.2.2-DIAG,
      2026-07-17) on an `EHEMTEST` MLDSA65 key: sign → alg `"MLDSA65"`,
      3309-byte signature; device verify → `EHEM_OK`; ctx round-trip OK
      and cross-ctx verify failed as required; MLDSA44 round-tripped with
      a 2420-byte signature; cleanup clean.
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-17):** an invalid ML-DSA
      signature produced **HTTP status 795** — a raw firmware error code
      in the status line, NOT the documented 406 (device > doc; garbage
      status confirmed, upstream filing = non-blocking follow-up). The
      SDK reported `EHEM_ERR_DEVICE` with the raw status retrievable,
      exactly per the defensive mapping; unit tests additionally pin
      65307/−229/100 → `EHEM_ERR_DEVICE`.
