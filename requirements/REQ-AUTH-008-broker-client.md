---
id: REQ-AUTH-008
title: Notification-broker client — cloud legs for ExtAuth pairing and login
status: verified
priority: must
revision: 6   # 2026-10-27 (STEP-M12-137 done): re-verified — the rev 5 unit criterion green on every CI leg (runs 39401189070 / 39401247238 on d532433, 39439574020 / 39439585486 on a481d07) and the attended one met: the user's `ext pair` from the published 1.2.0 candidates (rc3 on Linux, 2026-10-20; rc4 on Windows, reported 2026-10-27) scanned by Encedo HEM Auth 2.0.0-dev.8 paired; `ext list` shows the authenticator. No meaning change. Rev 5 = 2026-10-20 (STEP-M12-137): MEANING CHANGE — the QR payload is the SDK's (`ehem_ext_qr_payload()`) and its `hash` is defined: standard base64 of SHA-256 over the `request` string from ext/init, as the Manager composes it (assets/build.js:4827-4836) and hem-sdk-js (commit a750037); the placeholder "not_implemented_yet" that the new Encedo Auth app refuses is gone. §6.2 report approved with the plan 2026-10-20. needs-reverify until the unit criterion passes and the user's rc3 scan. Revs 2–4 = the live/attended facts marked inline below (2026-07-23 … 2026-08-06)
source: user decision 2026-07-22 (M8 decomposition); hem-api-tester test_5.php/test_6.php (notify flows); Encedo Manager encedo.js:335/1316, build.js:383/1438 (per doc-repo citations); NO doc page exists for the broker API — shapes are reconstructed, all pinned by live probes; the QR payload (rev 5): Encedo Manager assets/build.js:378-404 (`pair()`) and :4827-4836 (`pairdeviceNow`: `hash = base64(sha256(request))`, `JSON.stringify({link, hash, user, email, hostname})`), hem-sdk-js hem-sdk.js:1510-1518 (commit a750037, 2026-09-03), the user's bug report 2026-10-20 on v1.2.0-rc2 (the new Encedo Auth app: "the pairing link served a different request than the code promised"); user decision 2026-10-20: the SDK composes the payload
depends_on: ["REQ-NET-001", "REQ-NET-003", "REQ-AUTH-006", "REQ-AUTH-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#7-transport"]
---

# Notification-broker client — cloud legs for ExtAuth pairing and login

The SDK SHALL provide a client for the Encedo notification broker's
session, registration, and event endpoints, so pairing and push-confirm
login can be orchestrated without the caller speaking to the cloud
itself.

- **Base URL:** default `https://api.encedo.com/notify`
  (`EHEM_DEFAULT_NOTIFY_URL`), caller-overridable per call — the
  REQ-SYS-013 `register_url` precedent. All broker calls are
  unauthenticated, use the transport's absolute-URL path, and are ALWAYS
  fully TLS-verified (`EHEM_TLS_REQ_VERIFY`), like the check-in relay
  leg — regardless of the device-URL trust mode.
- **Endpoints** (reconstructed from tester + Manager; NO doc page —
  every shape below is an open criterion until live-pinned):
  - `/notify/session` → 200 `{epk, exp}` — broker Curve25519 pub for
    one pairing/login exchange. Two observed forms: POST `{"eid"}`
    (Manager, test_5 pairing) and a bodyless GET (test_6 login). The SDK
    binds BOTH: pairing uses POST-with-eid (the caller is authenticated
    and holds the eid); the login path (REQ-AUTH-009 `begin`) uses the
    GET form because mobile mode holds NO credentials and the eid may be
    unknown. **Live facts (rev 3, M8-080 session 2026-08-05):** sessions
    are CACHED server-side — repeat calls return the SAME epk until
    rotation; `exp` ≈ now + 24 h; the eid-POST form additionally returns
    `"paired": true/false` (whether that eid has any registered
    authenticators — a credential-free pairing probe). The two forms are
    NOT interchangeable: `event/new` accepts only the anonymous GET-form
    epk (an eid-bound epk → 404); `register/init` accepts only the
    eid-bound form (a GET-form epk → 401, rev 2).
  - `POST /notify/register/init` `{"epk", "eid", "request"}` → 200
    `{rid, link}` — starts a registration; `link` is the URL the phone
    app consumes (via QR).
  - `GET /notify/register/check/<rid>` → **202 = pending**, 200 =
    `{pid, reply}` once the phone completed its side.
  - `POST /notify/register/finalise/<rid>` — body = the device's
    `/ext/validate` response `{kid, code}` verbatim → 200.
  - `POST /notify/event/new` — body = the device's `/ext/request`
    response `{authreq, epk}` verbatim → 200 — pushes the confirmation
    request to every paired phone. **Live facts (rev 3):** the 200 body
    carries `eventid`, `sentcnt` (number of phones pushed — 0 would mean
    an unanswerable push, a future credential-free NOAUTH signal), and
    `ipinfo_you` — the broker GEOLOCATES the caller (ip/hostname/city/
    org/loc) and forwards it, presumably for the phone's approval UI;
    consumers should know the broker sees and shares this. **The broker
    VALIDATES the authreq's `iat` against its own clock with ~zero
    tolerance for the future**: 401 `{"err":"Cannot handle token prior
    to (iat …) <time>"}` — with the device RTC running ~8% fast
    (KNOWN-ISSUES), every mobile login 401s here within minutes of the
    last clock sync. This is the failure the REQ-AUTH-009 rev 2
    drift-gated check-in recovery exists for (discovered live at
    STEP-M8-080, 2026-08-05: first login green, second 401 — device was +51 s).
  - `GET /notify/event/check/<eventid>` → **202 = pending**; 200 with
    `authreply` = approved on the phone; 200 with `deny` set = rejected
    on the phone (tester note: the app only produces an `authreply` on
    ALLOW; on DENY the broker returns `deny=true` and the device is
    never contacted).
- Typed results own their strings (REQ-API-005 free conventions); 202 is
  surfaced as a distinct "pending" result, not an error. Broker error
  statuses map through the shared path with the response payload
  preserved in last-error detail.
- **The QR payload** (rev 5): what the phone scans is the JSON object
  `{"link","hash","user","email","hostname"}` — those keys in that order,
  compact — where `hash` is the standard base64 (padded) of SHA-256 over
  the bytes of the `request` string exactly as `ehem_ext_init` returned
  it. It is a commitment: the phone fetches `link`, hashes the request
  the broker serves and compares; a mismatch is refused ("the pairing
  link served a different request than the code promised" — the new
  Encedo Auth app, 2026-10-20; the older app did not check, which is why
  the tester's placeholder `"not_implemented_yet"` paired on 2026-08-05).
  The SDK composes it — **`ehem_ext_qr_payload(link, request, user,
  email, hostname, &json)`**, no context, byte-compatible with the
  Manager's `pairdeviceNow` (`JSON.stringify` of the same object: `/`
  unescaped, `"`/`\`/controls escaped; NULL config fields → `""`), the
  string freed with `ehem_notify_string_free` — because every consumer of
  the public registration legs needs it and the hash is the shim's.
  Rendering the payload (terminal QR) stays REQ-TOOL-016.

**Rationale:** the push leg cannot work device-locally — only the broker
reaches the phones. Keeping the broker client separate from the wait
engine (REQ-AUTH-009) keeps each piece independently testable: broker
legs against scripted fakes and gated live probes, the engine against a
fake broker. Broker behavior can change server-side without a firmware
update, hence the everything-is-an-open-criterion posture and the
`disruptive` gating of live probes (REQ-TEST-006, user decision
2026-07-22: never push to the user's real phone unattended).

**Acceptance criteria:**
- [x] Unit (fake transport, tests/unit/test_notify.c, 2026-07-23): each
      leg's request shape, 202-pending surfacing, approved/deny/pending
      discrimination on `event/check` (+ 200-with-neither → PROTOCOL),
      verbatim pass-through bodies, TLS always VERIFY on the wire,
      base-URL override honored, broker error payload preserved.
- [x] Live (test_notify_session_live, `integration`, 2026-07-23): BOTH
      session forms return 200 `{epk}` with a standard-base64 32-byte
      key (GET credential-free; POST with the eid harvested from an
      unauthenticated authreq's `iss`).
- [x] Live (test_notify_register_live, `disruptive`, 2026-07-23):
      `register/init` → 200 `{rid, link}` (64-char rid; link =
      `<base>/register/challenge/<rid>`), two `register/check` polls →
      202 pending. **Broker finding (rev 2): register/init REQUIRES an
      eid-BOUND session epk — one from the bodyless GET form is rejected
      HTTP 401 (payload not captured), so the pairing flow MUST use
      session-POST-with-eid.** Registration left dangling
      (expires broker-side). Phone leg → `{pid, reply}`, `finalise`
      status, and expired-`rid` behavior remain for STEP-M8-080.
- [x] Attended (real phone SM-S938B, STEP-M8-080, 2026-08-05/06): the
      phone-completed `register/check` 200 + `finalise` leg green (`ext
      pair` end-to-end via terminal QR); `event/check` shapes captured
      VERBATIM — pending 202 (empty), approved 200 `{authreply, 
      ipinfo_aid, ipinfo_eid}` (a consumed event keeps returning its
      result; the authreply stays single-use device-side via jti),
      denied 200 `{"deny":true}`, unknown/purged `rid`/`eventid` 404
      (an EXPIRED event converges to 404 — indistinguishable from
      unknown; the SDK's NOT_FOUND mapping is the sane terminal).
      Phone-app finding: the authreply stamps `exp = iat + 900`, so
      real-phone bearers live 15 min regardless of the authreq
      lifetime (the device copies authreply exp verbatim,
      REQ-AUTH-007).
- [x] Unit (tests/unit/test_notify.c `test_qr_payload`, rev 5 — local
      2026-10-20: `./dev ci` 56/56 gcc + clang, `./dev ci --openssl` 56/56
      gcc + clang, ASan both backends 56/56; the CI legs follow with the
      push):
      `ehem_ext_qr_payload` renders exactly
      `{"link":"https://l.ink/x","hash":"ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=","user":"Alice","email":"a@example.com","hostname":"hem.example"}`
      for request `"abc"` (the FIPS 180-2 known answer in base64 — the
      key order, compactness and the bare `/` proven by the exact
      string); `"r.j.w"` → `BQixETmS1vjcSjAPqjT0yq9poZjfeTnWczNU6VHYuI4=`
      with `"`/`\` in `user` escaped and NULL `email`/`hostname` → `""`;
      NULL link/request/out → `EHEM_ERR_ARG`; runs before any context
      exists (no `ehem_global_init`). Both backends, every CI leg.
- [x] Attended (rev 5): `ext pair` from the published `v1.2.0-rc3`
      hem-tool scanned by the NEW Encedo Auth app completes the
      registration (the user's report: date, app/phone, kid) — the only
      proof of the phone's check. **Met:** the user's rc3 report of
      2026-10-20 ("Finished tests on RC") named the Windows QR *display*
      as the only finding (REQ-TOOL-016 rev 4), and on 2026-10-27: "Ext
      pair works on windows" from the published `v1.2.0-rc4` hem-tool —
      app **Encedo HEM Auth 2.0.0-dev.8**, phone Samsung SM-S938B
      (Android); `hem-tool ext list` shows the one paired authenticator,
      kid `3dff0e4888f77b1e14e6812c17dbc4ce`, `[PROTECTED]`. The phone's
      hash check passed against the SDK's payload. The Manager's reference QR of
      2026-10-20 (decoded: the same five keys in the same order, a 44-char
      standard-base64 `hash`, `"email":""` for an empty field) is the
      shape evidence. *(Shape check 2026-10-20, dev build against the dev
      device: `hem-tool ext pair --no-qr` printed
      `{"link":"https://api.encedo.com/notify/register/challenge/<64>","hash":"<44-char base64>","user":"Debug HSM","email":"","hostname":"my.ence.do"}`
      — field for field the Manager's reference QR of the same device,
      only `link` and `hash` differing as they must; exit 3 after the
      60 s window, the registration left to expire broker-side.)*
