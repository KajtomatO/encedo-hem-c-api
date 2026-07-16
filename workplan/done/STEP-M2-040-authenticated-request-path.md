---
id: STEP-M2-040
title: "Authenticated request path: per-binding scope, bearer injection, single 401 re-acquire retry"
milestone: M2
implements: ["REQ-AUTH-003"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M2-030"]
evidence:
  commits: []   # implemented in the working tree; awaiting the user's commit
  tests:
    - "verifies: REQ-AUTH-003 — tests/unit/test_auth.c (6 new cases: scoped request carries Authorization: Bearer <token> and the login exchange carries none; 401→re-acquire→retry→AUTH_FAILED with the full 6-request sequence + fresh-connection retry with the NEW token; 401→retry→success; 403→SCOPE_DENIED with device payload and no retry; auth×check-in composition runs each recovery exactly once (10-request sequence); NULL-scope request sends no bearer and maps 401 straight through without retry)"
    - "verifies: REQ-AUTH-001/003 live — tests/integration/test_auth_live.c (gated on EHEM_TEST_URL + EHEM_TEST_PASSPHRASE): real login → ehem_auth_ensure_token → a parseable device-signed bearer whose scope claim == requested (keymgmt:list) + a recorded sub. GREEN and stable (5/5) against my.ence.do 2026-07-16: sub=U (User-key / PBKDF2 derivation). Deliberately does NOT assert same-scope cache reuse — see the device-quirk note below; cache reuse is proven deterministically in test_auth.c with a pinned clock."
  notes: |
    proto_common now carries the authenticated request path (REQ-AUTH-003).
    ehem_proto_request_raw/json gained a `const char *scope` parameter (threaded
    through all call sites: proto_system status/version/checkin and proto_auth's
    own login GET/POST all pass NULL — they are unauthenticated by construction).

    scope != NULL → ehem_auth_ensure_token(ctx, scope, &token) up front, then
    every send carries "Authorization: Bearer <token>" (built once per token via
    a small make_bearer helper; do_send took a bearer arg so the header rides the
    cert-recovery resend too). scope == NULL → no Authorization (status, version,
    check-in, login).

    401 retry (the ARCHITECTURE §7 "token re-acquisition" retry): a 401 on a
    scoped request → ehem_auth_invalidate(scope) (new; drops+scrubs that cache
    entry) → ehem_auth_ensure_token re-acquires → retry ONCE on a fresh
    connection. The retry is a bare do_send (no cert recovery) so it COMPOSES
    with but does not MULTIPLY the REQ-NET-005 check-in retry — at most one of
    each per request. A second 401 falls through to the mapping →
    EHEM_ERR_AUTH_FAILED. 403 → EHEM_ERR_SCOPE_DENIED with the device payload
    (map_http_status unchanged; the 401 case is intercepted before mapping only
    when a bearer was sent). Retry idempotency basis (REQ-AUTH-003 criterion 4):
    a 401 is rejected before the device processes the request, so re-sending is
    safe for every binding.

    Composition proof is a 10-request sequence test: login(2) → scoped send hits
    an expired cert → check-in recovery (3 legs) → resend (old token) → 401 →
    auth re-acquire (2) → retry (new token) → 200; asserts each recovery fired
    once and cert_refreshed is set.

    LIVE (my.ence.do, valid cert, no --insecure): the full C pipeline
    (PBKDF2-600k → X25519 keypair → ECDH → eJWT → POST) authenticates; the
    device echoes the requested scope and returns sub=U — closing the
    REQ-AUTH-001 open M2-gate "live authenticated round-trip + record sub" item.
    (Probed all of system:config / keymgmt:list / keymgmt:gen via the python
    client first: each echoes its scope, sub=U.) test_auth_live is an
    intentional exception to "integration tests use only the public API" — no
    public authenticated binding exists until M2-050, so it drives the internal
    ehem_auth_ensure_token and links the static lib (documented in the test +
    its CMake block); it skips (exit 77) without EHEM_TEST_URL/PASSPHRASE.

    SHORT-TOKEN FINDING (found running IT, 2026-07-16; root-caused in FIRMWARE
    source, corrected from a first imprecise reading): the issued bearer's TTL is
    ~59 s, BELOW the 60 s cache skew, so a just-issued token sits at/past its skew
    boundary and every authenticated call effectively re-logins. NOT a firmware
    "60 s token" design and NOT an SDK bug — it is caused by the CLIENT capping
    the eJWT exp at challenge.exp. Firmware ground truth (encedo_firmware):
      - gen_auth_token() (crypto.c): challenge `exp` is hardcoded ts+60 — a
        SUBMIT deadline (also backed by the time-based `jti` nonce), not a token
        lifetime.
      - api_post_auth_token() (api_auth.c:252-299): reads exp FROM our eJWT
        (exp_ts = jwt_get_grant_int(jwt,"exp")) and passes it to
        grant_jwt_auth_token(); no check that eJWT exp <= challenge.exp.
      - grant_jwt_auth_token() (crypto.c:3109): `if (exp==0) exp = ts +
        AUTH_TOKEN_LIFETIME;` else uses the client exp as-is. main.h:127
        AUTH_TOKEN_LIFETIME = 8*60*60 → the INTENDED default bearer life is 8 h
        (granted when the client sends exp==0; e.g. the Master path api_auth.c:742
        calls grant_jwt_auth_token(..., 0)).
    So the device would issue an 8 h token if we sent exp==0 (or a longer exp);
    we get ~59 s only because ehem_ejwt_build sets exp = min(requested,
    challenge.exp) = min(now+3600, now+60) = now+60 (the python reference client
    caps identically — its cache_exp came out now+2 s, same behavior). Whether to
    stop capping (long-lived tokens, fewer round-trips) vs. stay byte-exact with
    the python reference + REQ-AUTH-001's min(...) rule is a SPEC decision →
    deferred to STEP-M2-045 (auth token lifetime policy). M2-040 stays faithful
    to the reference here.
    Test consequence: a live same-scope cache-reuse assertion is a race on
    device/host clock skew, so test_auth_live only asserts the login round-trip;
    the initial version's reuse assert both flaked AND read a token pointer freed
    by the in-place cache replace (the borrowed-pointer contract) — removed.

    Verified: ./dev ci → gcc + clang 12/12 under -Werror; ./dev test asan →
    ASan/LSan clean (bearer heap buffers + cache invalidation leak-free across
    all retry/compose paths); export_symbols + public_headers_dep_free green
    (only ehem_login/ehem_logout added earlier; the scope param and
    ehem_auth_invalidate are internal/hidden). test_auth_live GREEN live and
    SKIPPED cleanly without creds.
reopened: []
cancelled: null
---

**Goal:** `ehem_proto_request_raw/json` (proto_common) accept an optional
scope: when non-NULL, obtain a token via `ehem_auth_ensure_token` and send
`Authorization: Bearer`. On 401 with a cached token: drop the cache entry,
re-login once, retry once; second 401 → `EHEM_ERR_AUTH_FAILED`. 403 →
`EHEM_ERR_SCOPE_DENIED`. Existing unauthenticated bindings (status,
version, checkin) pass NULL scope and are unchanged.

**Notes:** Composition guard with REQ-NET-005: the auth retry and the
check-in retry must not multiply (max one of each per request; assert via
sequence-counting unit test). Also add the first live integration test of
M2: `test_auth_live` — login + a scoped call (config GET arrives in
M2-050, so use token acquisition itself: assert `ensure_token` returns a
parseable JWT with the requested scope claim), gated on EHEM_TEST_URL +
EHEM_TEST_PASSPHRASE per REQ-TEST-002.

**Definition of done**
- [x] Fake-transport unit tests: bearer present for scoped requests,
      absent for NULL-scope; 401→re-login→retry→AUTH_FAILED sequence;
      403→SCOPE_DENIED with payload; auth×checkin retry composition guard.
- [x] `test_auth_live` green against the dev device (records token `sub`
      claim into REQ-AUTH-001 open criterion at the gate). GREEN 2026-07-16:
      sub=U against my.ence.do.
- [x] ASan/LSan + export checks green on GCC + Clang.
