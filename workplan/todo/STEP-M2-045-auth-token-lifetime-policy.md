---
id: STEP-M2-045
title: "Auth token lifetime policy: stop capping eJWT exp at the challenge submit-deadline so bearers are long-lived"
milestone: M2
implements: ["REQ-AUTH-001", "REQ-AUTH-002"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-040"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Make authenticated sessions acquire **long-lived** bearer tokens
instead of re-logging-in on almost every call. Root cause (firmware-confirmed
in STEP-M2-040): the client caps the eJWT `exp` at `challenge.exp`, but on this
device `challenge.exp` is a hardcoded **now+60 s submit-deadline**
(`gen_auth_token` in `crypto.c`; also enforced by the time-based `jti` nonce),
NOT a token-lifetime bound. The firmware copies our eJWT `exp` verbatim into the
issued bearer (`api_post_auth_token`, `api_auth.c:252-299`) with no
`exp ≤ challenge.exp` check, and its intended default is **8 h**
(`grant_jwt_auth_token` uses `ts + AUTH_TOKEN_LIFETIME`, `AUTH_TOKEN_LIFETIME =
8*60*60`, `main.h:127`) when the client sends `exp == 0`. So capping at
`challenge.exp` forces ~59 s tokens and (with the 60 s cache skew) re-login per
request.

Change `ehem_ejwt_build` / the login derivation so the requested token lifetime
is honored: request `exp = now + AUTH_TOKEN_LIFETIME` (drop the
`min(requested, challenge.exp)` cap) — or send `exp = 0`/omit it to take the
device's 8 h default. Pick one after confirming device acceptance live.

**Notes / decisions to make:**
- **Byte-exact reference tension (REQ-MGMT §8):** the python reference client
  (`build_ejwt`) ALSO caps at `challenge.exp`, so `ejwt_login_vector.h` was
  captured from a capping client. Removing the cap breaks the byte-exact match.
  Decide: (a) re-baseline the fixture to a hand-computed vector for the new
  rule and record the divergence from the python client in REQ-AUTH-001 /
  REQ-MGMT (the python client is arguably wrong here — it inherits the same
  re-login-every-call problem its own comment (MVP-OQ-2) claims to have fixed),
  or (b) fix the python client first and re-capture. Recommend (a) + file the
  finding upstream.
- **REQ-AUTH-001** step 4 says `exp: min(requested, challenge.exp)`. Amend to
  the new rule; keep `jti`/`aud` (the challenge contract) unchanged — the submit
  deadline is still enforced by `jti` (`nonce_validate_time_based`), so the POST
  must still happen within ~60 s; only the *token* lifetime changes.
- **REQ-AUTH-002:** with real 8 h tokens the existing "decode bearer exp, minus
  60 s skew" cache logic starts doing something useful (long cache, occasional
  silent refresh); no cache-code change expected, but re-verify.
- Confirm live that the device accepts an eJWT with `exp = now + 8h` (or
  `exp==0`) and returns a bearer with the long exp (probe both; `jwt_validate`
  in firmware has no upper exp bound, but verify empirically).
- Consider exposing the requested lifetime as an `ehem_options` field
  (append-only) if a caller wants short tokens — likely YAGNI for M2; note only.

**Definition of done**
- [ ] `ehem_ejwt_build` (or the auth layer) no longer caps the requested token
      lifetime at `challenge.exp`; unit tests updated (new fixture/vector, exp
      rule) and the REQ-AUTH-001 exp rule amended with the firmware rationale.
- [ ] Live: an authenticated session performs ONE login then reuses the cached
      bearer across multiple scoped calls (assert request count via a live or
      fake-with-long-token test); the device returns a long-lived bearer.
- [ ] Divergence from the python reference client recorded (REQ-AUTH-001 /
      REQ-MGMT); upstream finding filed/noted.
- [ ] `./dev ci` + asan + export/header gates green.
