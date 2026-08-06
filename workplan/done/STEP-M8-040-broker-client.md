---
id: STEP-M8-040
title: Notification-broker client — session, register, event legs
milestone: M8
implements: ["REQ-AUTH-008"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#7-transport"]
depends_on: ["STEP-M8-030"]
evidence:
  commits: ["3ca9a9c"]
  tests: ["verifies: REQ-AUTH-008 — tests/unit/test_notify.c (5 cases: leg shapes, 202-pending, event pending/denied/approved + unknown-shape PROTOCOL, verbatim pass-through, TLS VERIFY, base override, payload preserved); tests/integration/test_notify_session_live.c (both session forms live); tests/disruptive/test_notify_register_live.c (register/init + 202 polls live)"]
  notes: "src/proto_notify.c drives the transport DIRECTLY (not the device request path) because 202 is a result, not an error, and callers need the status; always EHEM_TLS_REQ_VERIFY, no pacing/recovery/bearer. LIVE: session GET + POST-with-eid both 200 {epk} (b64-32); register/init 200 {rid(64ch), link=<base>/register/challenge/<rid>}; 2× register/check 202. BROKER FINDING (REQ-AUTH-008 rev 2): register/init 401s a GET-form session epk — registrations require the eid-BOUND session (POST {eid}); pairing flow must use that form (test_5 concurs). Dangling rid left to expire broker-side (by design). Unit 35/35 gcc+clang+ASan; export/header gates green; proto_notify.c MinGW cross-syntax clean. Phone-leg/finalise/expired-rid + event shapes stay for M8-080."
reopened: []
cancelled: null
---

**Goal:** the six broker legs (`session` GET+POST forms,
`register/init|check|finalise`, `event/new|check`) as typed SDK calls on
the absolute-URL transport path, always `EHEM_TLS_REQ_VERIFY`, base URL
overridable (`EHEM_DEFAULT_NOTIFY_URL`), 202-pending surfaced as a
distinct non-error result.

**Notes:** first absolute-URL GETs through the cloud path — confirm the
transport plumbing (check-in/tls-recover only POST). `event/check`
discrimination: 202 pending / 200+`authreply` approved / 200+`deny`
rejected; pass `register/finalise` and `event/new` bodies through
VERBATIM (the REQ-SYS-013 splice pattern). Live: `notify/session` probe
under the plain `integration` label; `register/init` + a few
`register/check` 202 polls under the `disruptive` label (creates a
dangling broker registration — the rid is never finalised; note it in
the test). NO `event/new` live call in this step (needs an authreq and
would push once a phone is paired — attended-only, M8-080).

**Definition of done**
- [x] Unit: all legs' shapes, 202/200/deny discrimination, verbatim
      pass-through, TLS always-verify, base-URL override (fake
      transport)
- [x] Live `notify/session` (integration label) green; status + shape
      recorded in REQ-AUTH-008 (both forms)
- [x] Live `register/init`→`check` 202 polling (disruptive label) green;
      shapes + the eid-bound-session 401 finding recorded
- [x] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
