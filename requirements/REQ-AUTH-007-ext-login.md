---
id: REQ-AUTH-007
title: ExtAuth login bindings — request and token (push-confirm bearer issuance)
status: verified
priority: must
revision: 2
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
  check-in-and-retry recovery exactly once per binding call — the
  REQ-AUTH-004 recovery pattern (whose budget is likewise per
  acquisition call, not per context; rev 2 wording fix), honoring
  `no_auto_checkin` and the check-in recursion guard (the wiped dev
  device boots with RTC unset — KNOWN-ISSUES).
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
- [x] Unit (fake transport, tests/unit/test_ext.c, 2026-07-23):
      request/token shapes; arg pre-validation (scope length, ctx/note
      bounds, epk length); 403-RTC exactly-one checkin+retry (second
      403 terminal; `no_auto_checkin` suppresses); 401/406 →
      `EHEM_ERR_AUTH_FAILED` with distinguishing detail; token returned
      verbatim; no Authorization header and no login exchange on the
      wire.
- [x] Live (test_ext_login_live, 2026-07-23): pair → request → decrypt
      own scheme-A entry → build authreply → token; the bearer
      AUTHENTICATED a real `GET /api/system/config` (200, devid); bearer
      `sub` = base64(kid), `exp` = the authreply's exp, `ctx` echoed —
      all asserted; a tampered authreply → 401 `EHEM_ERR_AUTH_FAILED`.
- [x] Live (same run): `keymgmt:use:<kid>` rewrite observed — decrypted
      entry = `<scope>#<base64 {"t":…,"l":…}>` (label matched), authreq
      `exp−iat` = 900 s; plain scope = 3600 s.
- [x] Live probes (2026-07-23): both endpoints answered 200 on a
      NEVER-logged-in context with no Authorization header (bypass
      confirmed despite the missing routing source); zero-pairing
      authreq captured — 200 with `"scope": {}` (curl probe). A REAL
      authreq + opening keys frozen as
      tests/support/fixtures/ext_authreq_fixture.h; the unit suite
      verifies + decrypts it offline. NB the firmware's JWT header key
      order is `{"ecdh","typ","alg"}` (libjwt), differing from the
      login builder's — irrelevant to verification, recorded for
      parser-writers.
