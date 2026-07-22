---
id: REQ-AUTH-007
title: ExtAuth login bindings — request and token (push-confirm bearer issuance)
status: approved
priority: must
revision: 1
source: user decision 2026-07-22 (M8 decomposition); encedo-hem-api-doc auth/ext-request.md, ext-token.md; encedo_firmware api_auth.c:1289 (alter_requested_scope), :1329 (request), :1588 (token), crypto.c:3177 (grant_ext_jwt_auth_token); hem-api-tester test_6.php
depends_on: ["REQ-AUTH-004", "REQ-AUTH-006", "REQ-SYS-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
---

# ExtAuth login bindings — request and token (push-confirm bearer issuance)

The SDK SHALL provide bindings for the two unauthenticated ExtAuth login
endpoints — `ehem_ext_request` (emit an `authreq` every paired
authenticator can decrypt) and `ehem_ext_token` (exchange a countersigned
`authreply` for a bearer).

- **Access (both, fw api_auth.c):** NO bearer parsed or required (doc:
  JWT bypass list — the fw routing file is absent from the checkout, so
  this is confirmed by tester/Manager usage + live probe). Preconditions
  in order: RTC set (else **403**), initialised (else 409), `fls_state==0`
  (else 409). On 403 the SDK SHALL run the single
  check-in-and-retry recovery exactly once, sharing the per-context
  recovery budget of REQ-AUTH-004 (the wiped dev device boots with RTC
  unset — KNOWN-ISSUES).
- **`ehem_ext_request(ctx, epk_b64, scope, ctx_str, note, &out)`** →
  POST `/api/auth/ext/request` `{"epk", "scope", "ctx"?, "note"?}`;
  200 → `{authreq, epk}` (`ehem_ext_request_info`). Firmware facts:
  - `scope` ≤1023 bytes (400 above); `ctx` 1–64 chars and `note` 1–128
    chars, silently dropped outside those bounds (fw malloc-guarded
    copies) — the SDK pre-validates and returns `EHEM_ERR_ARG` instead.
  - **Scope rewrite** (api_auth.c:1289): a scope of exactly
    `keymgmt:use:<32-hex-kid>` gets `#<base64({"t":"<type_hex>","l":
    "<label>"})>` appended (so the phone can display the key) and the
    token lifetime drops 60 min → **15 min**. Any other scope: 60 min,
    no suffix. The `#`-suffix is stripped by `/ext/token` before the
    bearer is minted.
  - `authreq` JWT: HS256 over `ECDH(EIDkey, epk)`; grants `iss`=eid,
    `aud`=epk, `iat`, `exp`=iat+lifetime, `jti`=base64(32-byte time
    nonce), `ctx`/`note` echoed, and `scope` = an **object**: one entry
    per paired authenticator (≤8), key = base64(32-byte descriptor
    suffix), value = `"A" + base64(AES128-CBC ciphertext ‖ 32-byte HMAC
    trailer)` — scheme A, REQ-TEST-006 records the exact construction.
    Zero paired authenticators still yields 200 with an empty `scope`
    object.
  - **Divergence (tester vs fw):** test_6.php sends an `exp` field in
    the request body; fw v1.2.2 never reads it — lifetime is fixed
    60/15 min. Recorded here so nobody "restores" that field.
- **`ehem_ext_token(ctx, authreply_jwt, &token)`** → POST `/ext/token`
  `{"authreply"}`; 200 → `{token}`. Firmware facts:
  - `authreply` must be HS256-signed with `ECDH(authenticator_priv,
    EIDkey_pub)`; grants: `jti` (echo of `authreq.jti`, revalidated),
    `pid` (locates the EXTAID key; its stored pubkey must byte-match
    `iss`), `scope` (scheme-`A` ciphertext of the approved scope, same
    per-authenticator key derivation `HMAC-SHA256(key=ECDH, msg=jti)` →
    AES key = K[0..15], IV = K[16..31], trailer = HMAC(key=K, msg=
    unpadded scope)), optional `ctx`, and `exp`.
  - The issued bearer (crypto.c:3177): `sub` = **base64(kid)** of the
    approving authenticator (not `"U"`/`"M"`), `scope` = decrypted scope
    (`#`-suffix stripped), `exp` taken VERBATIM from the authreply's
    `exp` grant (`0` → fw fallback now+8 h, same `AUTH_TOKEN_LIFETIME`
    as password login), fresh `jti`, HS256 with the per-boot session
    key. On the wire it is indistinguishable from a password-login
    bearer; downstream scope enforcement is identical.
  - Errors: 400 malformed; 401 JWT decode/validate/jti fail; 403 RTC
    unset; 406 unknown `iss`/`pid`, scheme not `"A"`, decrypt or HMAC
    trailer mismatch; 409 state. SDK maps 401 AND 406 →
    `EHEM_ERR_AUTH_FAILED` with detail (406 here means "reply not
    acceptable", not a scope problem).
  - **Firmware finding:** the documented anti-bruteforce delay
    (`auth_delay_response_remote`, api_auth.c:71) is dead code — it
    early-returns whenever `start_ts < ts`, i.e. on any request taking
    ≥1 ms tick, so no failure delay is actually applied. Do not design
    SDK timeouts around it. Upstream filing candidate.

**Rationale:** this is the only auth path issuing a bearer without the
KDF passphrase (HEM-SDK-2 mobile mode). The bindings are deliberately
low-level and broker-free: REQ-AUTH-009 composes them with the broker
client (REQ-AUTH-008), and REQ-TEST-006's simulated authenticator drives
them device-locally.

**Acceptance criteria:**
- [ ] Unit (fake transport): request/token shapes; arg pre-validation
      (scope length, ctx/note bounds, epk length); 403-RTC single
      checkin+retry sharing the AUTH-004 budget; 401/406 →
      `EHEM_ERR_AUTH_FAILED`; 409 → `EHEM_ERR_DEVICE`.
- [ ] Live (simulated authenticator, REQ-TEST-006): pair → request →
      decrypt own scheme-A entry → build authreply → token; bearer used
      on `GET /api/system/config` successfully; bearer `sub` =
      base64(kid) recorded.
- [ ] Live: `keymgmt:use:<kid>` scope rewrite observed (`#`-suffix in
      the decrypted entry, 15-min `exp` in authreq and issued bearer);
      60-min lifetime for a plain scope.
- [ ] Open (live probe): both endpoints confirmed reachable with no
      Authorization header (routing source missing); empty-`scope`
      object shape with zero paired authenticators captured.
