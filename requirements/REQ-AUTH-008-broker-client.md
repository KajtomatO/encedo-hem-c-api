---
id: REQ-AUTH-008
title: Notification-broker client — cloud legs for ExtAuth pairing and login
status: approved
priority: must
revision: 3
source: user decision 2026-07-22 (M8 decomposition); hem-api-tester test_5.php/test_6.php (notify flows); Encedo Manager encedo.js:335/1316, build.js:383/1438 (per doc-repo citations); NO doc page exists for the broker API — shapes are reconstructed, all pinned by live probes
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
    unknown. **Live facts (rev 3, M8-080 session 2026-07-23):** sessions
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
    STEP-M8-080: first login green, second 401 — device was +51 s).
  - `GET /notify/event/check/<eventid>` → **202 = pending**; 200 with
    `authreply` = approved on the phone; 200 with `deny` set = rejected
    on the phone (tester note: the app only produces an `authreply` on
    ALLOW; on DENY the broker returns `deny=true` and the device is
    never contacted).
- Typed results own their strings (REQ-API-005 free conventions); 202 is
  surfaced as a distinct "pending" result, not an error. Broker error
  statuses map through the shared path with the response payload
  preserved in last-error detail.
- The QR payload is NOT a broker concern: what the phone scans is a JSON
  object `{link, hash, user, email, hostname}` composed by the CALLER
  (tester test_5.php:90; `hash` literally `"not_implemented_yet"`). The
  broker client exposes `rid`/`link`; composing and rendering the QR is
  REQ-TOOL-016.

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
- [ ] Open (attended, real phone — STEP-M8-080): `event/new` → poll →
      the THREE terminal shapes captured verbatim (approved `authreply`,
      `deny` on reject, and what the broker returns after the authreq
      `exp` passes); the phone-completed `register/check` 200 and
      `finalise` leg; recorded here and in KNOWN-ISSUES if surprising.
