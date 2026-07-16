---
id: STEP-M2-030
title: "ehem_login/ehem_logout: challenge flow, token cache + silent refresh, RTC-unset recovery, auth error mapping"
milestone: M2
implements: ["REQ-AUTH-001", "REQ-AUTH-002"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `src/proto_auth.c` + public `include/ehem/auth.h`:
`ehem_login(ctx, passphrase)` (challenge GET → derive → eJWT POST → cache
token), `ehem_logout(ctx)` (zeroize credentials, drop cache), internal
`ehem_auth_ensure_token(ctx, scope, &token)` for later bindings.
Scope-keyed token cache honoring the token's own `exp` claim (fallback
3600 s) minus 60 s skew. Passphrase retained (zeroized copy) for silent
refresh unless the new `ehem_options.no_credential_retention` is set
(append-only options discipline). Challenge GET 403 (RTC unset) triggers
one check-in + retry unless `no_auto_checkin` (REQ-SYS-003/NET-005
machinery reused).

**Notes:** `ehem_login` acquires no token itself unless given a scope to
prime — decide: prime nothing and let the first binding acquire lazily
(recommended; matches python client `ensure_token`). Auth failures: POST
401 → `EHEM_ERR_AUTH_FAILED` + device payload in last-error; expired with
retention off → `EHEM_ERR_AUTH_EXPIRED` (client-side, no network). Record
the challenge `lbl` (username) on the ctx for debug/last-error detail —
optional, no public API yet.

**Definition of done**
- [ ] Fake-transport unit tests: full login sequence (no Authorization on
      challenge GET, eJWT in POST body, claim exp =
      min(now+3600, challenge.exp)); 401 mapping; RTC-unset 403 →
      check-in → retry (and opt-out via no_auto_checkin).
- [ ] Cache unit tests per REQ-AUTH-002: same-scope reuse, per-scope
      isolation, skew-window re-acquisition, device-shortened exp
      honored, retention-off → AUTH_EXPIRED without network, logout drops
      cache + zeroizes.
- [ ] `ehem_ctx_destroy` zeroizes credential material (LSan/ASan clean).
- [ ] New public header exports only `ehem_*`; export check green.
