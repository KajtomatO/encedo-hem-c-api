---
id: STEP-M8-040
title: Notification-broker client — session, register, event legs
milestone: M8
implements: ["REQ-AUTH-008"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#7-transport"]
depends_on: ["STEP-M8-030"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Unit: all legs' shapes, 202/200/deny discrimination, verbatim
      pass-through, TLS always-verify, base-URL override (fake
      transport)
- [ ] Live `notify/session` (integration label) green; status + shape
      recorded in REQ-AUTH-008
- [ ] Live `register/init`→`check` 202 polling (disruptive label) green;
      shapes recorded
- [ ] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
