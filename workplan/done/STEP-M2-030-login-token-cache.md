---
id: STEP-M2-030
title: "ehem_login/ehem_logout: challenge flow, token cache + silent refresh, RTC-unset recovery, auth error mapping"
milestone: M2
implements: ["REQ-AUTH-001", "REQ-AUTH-002"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-020"]
evidence:
  commits:
    - "a0da701 — STEP-M2-030 login + token cache (proto_auth, public auth.h, options.no_credential_retention, test_auth)"
  tests:
    - "verifies: REQ-AUTH-001, REQ-AUTH-002 — tests/unit/test_auth.c (13 cases: full login GET→POST sequence with no Authorization on the challenge GET and the eJWT POST body byte-exact vs the python fixture; exp = min(now+lifetime, challenge.exp) incl. the challenge-capped case; POST 401 → EHEM_ERR_AUTH_FAILED with device payload; RTC-unset 403 → check-in → retry + no_auto_checkin opt-out; cache same-scope reuse, per-scope isolation, 60s-skew re-acquire, device-shortened exp honored; retention-off → AUTH_EXPIRED without network; logout drops cache; re-login resets cache; arg validation)"
  notes: |
    src/proto_auth.{h,c} (implements REQ-AUTH-001 login flow + REQ-AUTH-002
    cache) + public include/ehem/auth.h (ehem_login / ehem_logout). Added
    proto_auth.c to EHEM_SOURCES; auth.h installs via the include/ glob.

    Session state (struct ehem_auth, opaque behind ctx->auth, defined in
    proto_auth.c so credential material never leaves the auth component):
    retained passphrase copy + retain flag + last challenge username + a
    singly-linked scope→{token,exp} cache. ctx gained `struct ehem_auth *auth`
    (forward-declared in context.h) and `no_credential_retention` (mirrors the
    new append-only ehem_options field). ehem_ctx_destroy calls
    ehem_auth_destroy (implicit logout) BEFORE the struct memset.

    DESIGN DECISION (step said "decide"): ehem_login is lazy — it stores the
    credential and returns, no network (matches the python client's Auth:
    ensure_token does the work on first use). Token acquisition funnels through
    the internal ehem_auth_ensure_token(ctx, scope, &token) (the chokepoint
    M2-040's authenticated bindings will call): cache hit outside the 60s skew →
    return; else GET /api/auth/token → PBKDF2(passphrase, salt=eid) → X25519
    keypair → ECDH(spk) → ehem_ejwt_build → POST {"auth":<ejwt>} → cache the
    bearer. Cache expiry honors the returned bearer's own exp claim when
    readable (decode_bearer_exp, base64url payload seg — mirrors the python
    client's device-shortened-lifetime handling), else now+3600, minus 60s.

    RETENTION (REQ-AUTH-002): default retains the passphrase for the ctx
    lifetime (silent refresh). no_credential_retention scrubs it right after the
    first successful acquisition, so any later miss/expiry → EHEM_ERR_AUTH_EXPIRED
    with NO network, forcing a fresh ehem_login (documented in auth.h). Chose
    scrub-after-first-use (earliest safe point given lazy login) over keeping the
    passphrase alive — minimises its in-memory lifetime, the point of the opt-out.

    RTC-unset recovery (REQ-AUTH-001 / reuses REQ-SYS-003 machinery): a 403 on
    the challenge GET means the device clock is unset; fetch_challenge runs
    ehem_checkin_run once (device legs relaxed, same posture as ehem_system_checkin
    — the payloads are cloud-signed) then retries the GET once, guarded by
    no_auto_checkin + ctx->in_checkin (mirrors REQ-NET-005, keyed on 403 not TLS).
    Copied the check-in's last-error message to a local buffer before ehem_ctx_fail
    to avoid aliasing the shared err buffer through %s (same pattern proto_common
    uses for the cert-recovery path).

    Zeroization: seed / private scalar / shared secret scrubbed after the eJWT
    build; the signed eJWT and the POST body scrubbed before free; cached tokens
    and the passphrase scrubbed on replace / logout / destroy.

    Test seam: a file-scope, test-only clock override (ehem_auth_test_set_clock,
    declared in proto_auth.h, hidden visibility — not in the export table) lets
    the unit test pin "now" to EJWT_FX_NOW so the emitted eJWT byte-matches the
    M2-020 fixture through the whole pipeline, and craft exp windows. Crafted
    bearer tokens are hdr.<base64url({"exp":N,"k":tag})>.sig; the tag makes a
    re-acquisition observable when exp is unchanged.

    Verified on the dev machine:
      - ./dev ci → gcc + clang, 12/12 unit tests each under -Werror.
      - ./dev test asan → ASan/LSan clean (credential/token/secret scrubs and
        cache teardown leak-free across all 13 cases).
      - export_symbols green (only ehem_login/ehem_logout added; ensure_token,
        the clock seam, and ehem_auth_* stay internal/hidden).
      - public_headers_dep_free green (auth.h is libcurl/wolfSSL-free).
    Live authenticated round-trip is intentionally deferred: it is M2-040's DoD
    (test_auth_live) and the REQ-AUTH-001 open M2-gate criterion.
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
- [x] Fake-transport unit tests: full login sequence (no Authorization on
      challenge GET, eJWT in POST body, claim exp =
      min(now+3600, challenge.exp)); 401 mapping; RTC-unset 403 →
      check-in → retry (and opt-out via no_auto_checkin).
- [x] Cache unit tests per REQ-AUTH-002: same-scope reuse, per-scope
      isolation, skew-window re-acquisition, device-shortened exp
      honored, retention-off → AUTH_EXPIRED without network, logout drops
      cache + zeroizes.
- [x] `ehem_ctx_destroy` zeroizes credential material (LSan/ASan clean).
- [x] New public header exports only `ehem_*`; export check green.
