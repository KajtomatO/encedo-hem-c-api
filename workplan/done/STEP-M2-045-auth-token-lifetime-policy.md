---
id: STEP-M2-045
title: "Auth token lifetime policy: stop capping eJWT exp at the challenge submit-deadline so bearers are long-lived"
milestone: M2
implements: ["REQ-AUTH-001", "REQ-AUTH-002"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-040"]
evidence:
  commits: []   # implemented in the working tree; awaiting the user's commit
  tests:
    - "verifies: REQ-AUTH-001 — tests/unit/test_ejwt.c (test_ejwt_matches_python_fixture still byte-exact after the cap removal — the fixture's requested_exp < challenge_exp so capped == uncapped; test_ejwt_exp_and_args, renamed from _exp_cap_and_args, now asserts exp = requested verbatim incl. a far-future value)"
    - "verifies: REQ-AUTH-001 — tests/unit/test_auth.c (test_login_exp_is_requested_lifetime, renamed from _exp_capped_by_challenge: a now+10 challenge deadline does NOT shorten the token; the eJWT exp is now+3600)"
    - "verifies: REQ-AUTH-002 live — tests/integration/test_auth_live.c: the live bearer TTL is now ~3600 s (3598 s observed, was ~59 s), and same-scope reuse reliably returns the SAME cached token (re-added now that TTL ≫ skew). GREEN + 3/3 stable against my.ence.do."
  notes: |
    Firmware-confirmed root cause + fix for the ~60 s tokens / re-login-per-call
    found in STEP-M2-040. Empirically verified against my.ence.do BEFORE coding
    (raw eJWT probes via the python client's crypto, custom exp):
      - eJWT exp = now+8h  → bearer TTL 28799 s (device honors it);
      - eJWT exp = now+3600 → bearer TTL 3600 s;
      - eJWT exp = 0        → HTTP 401 (the eJWT is then self-expired → rejected
        at the device's jwt_validate; the firmware's exp==0 → 8h fallback in
        grant_jwt_auth_token is only reachable from its internal Master path,
        api_auth.c:742, not from a client eJWT).

    Change: ehem_ejwt_build no longer caps `exp` at the challenge deadline —
    dropped the `challenge_exp` parameter entirely (internal API) and set
    `exp = requested_exp` verbatim. proto_auth.c stops parsing/passing
    challenge.exp (it requests now + AUTH_TOKEN_LIFETIME = now+3600; challenge
    `exp` is the submit deadline, enforced server-side by the jti nonce, so it is
    no longer read). AUTH_TOKEN_LIFETIME kept at 3600 (matches the python
    client's intended _TOKEN_LIFETIME_S; the device would grant up to 8 h).

    Byte-exact fixture PRESERVED: the M2-020 vector has requested_exp
    (1700003600) < challenge_exp (2000000000), so min(requested,challenge) ==
    requested == the uncapped value — the emitted eJWT is byte-identical.
    test_ejwt_matches_python_fixture and test_auth's byte-exact POST-body check
    both still pass unchanged. Only the two tests that DELIBERATELY exercised the
    cap were updated (they now assert exp = requested).

    DIVERGENCE from the reference python client (recorded per REQ-MGMT §8, in
    REQ-AUTH-001): its build_ejwt still caps at challenge.exp and thus re-logins
    per call on this firmware (its own MVP-OQ-2 "fix" using _decode_bearer_exp
    does not actually take effect — the decoded device exp is the ~60 s value).
    The C SDK is deliberately better here; the divergence only manifests when
    challenge.exp < requested (i.e. never in the byte-exact fixture). Filing the
    finding upstream to the python client is a follow-up (noted, not blocking).

    REQ-AUTH-001 exp rule amended (min → verbatim, with the firmware citations);
    REQ-AUTH-002 notes the cache now genuinely holds (TTL 3600 s ≫ 60 s skew);
    fixture header comment updated. NOT exposing a per-call lifetime option
    (YAGNI for M2). Verified: ./dev ci 12/12 gcc+clang under -Werror; ./dev test
    asan clean; export/header gates green; test_auth_live GREEN live (TTL 3598 s,
    3/3 stable) and skips cleanly without creds.
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
- [x] `ehem_ejwt_build` (or the auth layer) no longer caps the requested token
      lifetime at `challenge.exp`; unit tests updated (byte-exact fixture
      preserved — cap==nocap for its inputs; the two cap tests updated) and the
      REQ-AUTH-001 exp rule amended with the firmware rationale.
- [x] Live: a long-lived bearer is issued (TTL 3598 s, was ~59 s) and same-scope
      reuse returns the SAME cached token with no re-login
      (tests/integration/test_auth_live.c; 3/3 stable). Deterministic reuse
      counts are covered by test_auth.c (pinned clock).
- [x] Divergence from the python reference client recorded (REQ-AUTH-001 +
      REQ-MGMT §8 note); upstream filing noted as a non-blocking follow-up.
- [x] `./dev ci` + asan + export/header gates green.
