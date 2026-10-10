---
id: REQ-AUTH-001
title: Passphrase login — credential derivation and eJWT token acquisition
status: verified
priority: must
revision: 3
source: ARCHITECTURE.md §5; encedo-hem-api-doc auth/token.md; encedo-hem-python-api auth.py (build_ejwt — WORKING against the dev device, live-proven 2026-07-16); encedo-manager assets/build.js postAuthToken + pbkdf2KeyDerive (same derivation; minimal eJWT header, see Known discrepancy); rev 2 = M9 sweep record (2026-08-06, header-enforcement fact); rev 3 = KDF correction (2026-10-06: the Manager derives with PBKDF2, not Argon2; `sub` reading corrected; Argon2 unsupported — user decision 2026-10-05)
depends_on: ["REQ-API-001", "REQ-API-003", "REQ-API-004", "REQ-SYS-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
---

# Passphrase login — credential derivation and eJWT token acquisition

The SDK SHALL provide `ehem_login(ctx, passphrase)` implementing the HEM
challenge–response authentication flow:

1. `GET /api/auth/token` → challenge `{eid, spk, jti, exp, lbl?}`;
2. derive the user X25519 keypair from the passphrase (KDF below);
3. ECDH(user_private, `spk`) → shared secret;
4. build a compact eJWT — header `{"ecdh":"x25519","alg":"HS256","typ":"JWT"}`
   (hardcoded byte string, never re-serialized), claims
   `{jti, aud: spk, exp: now + requested lifetime, iat, iss: user
   public key (standard base64 WITH padding), scope}`, segments base64url
   WITHOUT padding, HMAC-SHA256 tag keyed with the raw shared secret;
5. `POST /api/auth/token {"auth": "<ejwt>"}` → `{token}` (scoped bearer).

**Token lifetime — `exp` is the requested lifetime, NOT capped at the
challenge (amended STEP-M2-045, firmware-confirmed):** the eJWT `exp` claim
is `now + AUTH_TOKEN_LIFETIME` (the client's requested lifetime, currently
3600 s) emitted verbatim. It is deliberately NOT `min(requested,
challenge.exp)`. The challenge `exp` is the response DEADLINE — enforced
server-side by the time-based `jti` nonce (`nonce_validate_time_based`) — and
on the dev device is a hardcoded `now+60 s` (`gen_auth_token`, `crypto.c`).
The firmware copies the eJWT `exp` straight into the issued bearer
(`api_post_auth_token`, `api_auth.c:252-299`; no `exp ≤ challenge.exp`
check), so capping at the ~60 s deadline yielded ~60 s bearers and a re-login
on almost every authenticated call. Live-proven 2026-07-16: requesting
`now+3600` returns a 3600 s bearer; `now+8h` returns a ~28800 s bearer;
`exp=0` is rejected (the eJWT would itself be expired at the device). NOTE:
this diverges from the reference python client's `build_ejwt`, which still
caps at `challenge.exp` (and thereby re-logins per call — its own MVP-OQ-2
fix does not actually take effect on this firmware); the byte-exact fixture
is unaffected because its inputs have `requested < challenge` (so capped ==
uncapped). Divergence recorded here per REQ-MGMT §8.

**KDF (pinned to the proven implementation):** PBKDF2-HMAC-SHA256,
**600 000 iterations**, output 32 bytes, salt = the challenge `eid` value
as its **raw UTF-8 base64 string** (NOT base64-decoded). This is the
python client's derivation and it authenticates against the dev-machine
HEM today (live-proven 2026-07-16: config write + reboot succeeded).

**Rationale:** the device stores only the user's X25519 public key
(UserKey), registered at init; login works iff the client re-derives the
same private key. The KDF is therefore fixed by how the target device was
initialised, not negotiable per session.

**Reference clients (corrected 2026-10-06, rev 3):** Encedo Manager
(`assets/build.js`: `pbkdf2KeyDerive`, called from `initFinal`,
`postAuthToken`, `updateCfg` and the password change — checked at
encedo-manager `b33c236`), the HEM API test suite
(`hem-api-tester/libs/lib.php`) and the python client all use the KDF
pinned above, so every one of them derives the same key from the same
passphrase; Manager login to the dev device with the test passphrase
succeeded on the same day `test_auth_live` passed (attended check,
2026-10-06). Revisions 1–2 of this requirement said the Manager derives
with Argon2 and that a Manager-initialised device would reject PBKDF2;
that described the Manager's legacy files (`assets/encedo.js`,
`assets/core2.js`), which the current Manager does not run. **Argon2 is
not supported** (user decision 2026-10-05). An older Manager release most
likely did derive with Argon2; a device initialised by one would reject
this login, and whether any such device exists is not known
(ARCHITECTURE.md §12 risk 2).

**Known discrepancy (recorded, not smoothed over):** the Manager sends a
minimal eJWT header `{"ecdh":"x25519"}` (`build.js:918-920`) where this
SDK sends `{"ecdh":"x25519","alg":"HS256","typ":"JWT"}`. Both header
forms pass because the firmware reads ONLY the `ecdh` header field —
`alg` and `typ` are documentation convenience, not validation gates
(DISCREPANCIES-HEM-TEST, recorded at the M9 sweep 2026-08-06).

**Acceptance criteria:**
- [ ] Crypto shim provides PBKDF2-HMAC-SHA256, HMAC-SHA256 and X25519
      (keypair from 32-byte seed + ECDH) with unit tests against published
      vectors (RFC 7748 §5.2 X25519; RFC 4231 HMAC; a PBKDF2-SHA256 vector).
- [ ] eJWT encoder reproduces byte-for-byte a fixture eJWT generated by the
      python client's `build_ejwt` from identical inputs (unit test).
- [ ] `ehem_login` performs GET→derive→POST against the fake transport;
      asserts request sequence, Authorization absent on the challenge GET,
      claim set and `exp = min(now+lifetime, challenge.exp)` (unit test).
- [ ] Auth failures map per REQ-API-003: POST 401 → `EHEM_ERR_AUTH_FAILED`
      with `ehem_last_error` detail (unit test).
- [ ] Challenge GET 403 (device RTC unset) triggers the check-in flow
      (REQ-SYS-003) once and retries the challenge, unless
      `no_auto_checkin` is set — mirrors REQ-NET-005 semantics (unit test).
- [ ] All secret intermediates (KDF output/private key, shared secret,
      retained passphrase copies) are zeroized after use; ASan/LSan clean.
- [x] RESOLVED (STEP-M2-040, 2026-07-16): live authenticated round-trip
      against my.ence.do via the C SDK (tests/integration/test_auth_live.c)
      succeeds — the device accepts the **PBKDF2-HMAC-SHA256** derivation and
      returns a bearer with **`sub` = `U`** (the token's `iss` matched the
      stored UserKey; `M` would mean the MasterKey — firmware
      `api_auth.c:262-265`. The letter says nothing about the KDF or the
      client that initialised the device; gloss corrected 2026-10-06). The
      requested scope is echoed verbatim in the token's `scope` claim
      (verified for keymgmt:list / keymgmt:gen / system:config). This
      confirms the §5 KDF decision (ARCHITECTURE §12 risk 2; text updated at
      the M2 gate, STEP-M2-070, and corrected 2026-10-06).
