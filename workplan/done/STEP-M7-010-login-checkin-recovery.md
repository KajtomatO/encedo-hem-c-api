---
id: STEP-M7-010
title: "Clock-drift login recovery + checkin_on_login option"
milestone: M7
implements: ["REQ-AUTH-004", "REQ-AUTH-005"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: []
evidence:
  commits: ["79b2d3b"]
  tests: ["verifies: REQ-AUTH-004 (tests/unit/test_auth.c — 4 drift cases), REQ-AUTH-005 (test_auth.c — 2 option cases; tests/integration/test_auth_live.c — live proactive check-in green 2026-07-18)"]
  notes: >
    Unit 29/29 gcc+clang + ASan clean; export/header gates green; MinGW
    cross-syntax check OK on context.c + proto_auth.c. Drift gate: challenge
    exp − 60 vs local now beyond ±60 s (challenge_drift_evident); recovery
    shares ONE per-ensure_token budget with the challenge-403 path
    (recovery_spent), so the two check-in recoveries compose (unit-proven).
    Wrong-passphrase 401 with a synced clock spends zero check-ins.
    checkin_on_login latches (done-before-attempt) → at most one attempt per
    context even on failure; failure leaves a message-only breadcrumb, rc OK.
    Ripple: test_cert_install's CHALLENGE_JSON fixture had a far-future exp
    that now reads as drift → switched to a synced exp (now+60).
    LIVE (my.ence.do, 2026-07-18): checkin_on_login context ran the 3-leg
    check-in + login green (no breadcrumb). The device was ~24 min AHEAD at
    first run — device-stamped TTL shrank to 2164 s and failed the ttl>=3000
    assert; reordered the suite so the proactive check-in runs FIRST (clock
    resync), then 2/2 green — live demonstration of exactly the pathology
    these REQs heal. Drift-401 recovery itself is unit-proven; live it fires
    opportunistically (drift was < TTL today, so logins succeeded without it).
reopened: []
cancelled: null
---

**Goal:** proto_auth heals the ~8%-fast RTC drift: a login-POST 401 with
drift evidence (challenge `exp` − 60 vs local now beyond the 60 s skew)
triggers one `ehem_checkin_run` + one retry (REQ-AUTH-004); new
append-only `ehem_options.checkin_on_login` runs one best-effort
check-in before the context's first token acquisition (REQ-AUTH-005).

**Notes:** First M7 step on purpose — it makes every later live leg
immune to the drift-401 mode (KNOWN-ISSUES "Device clock runs ~8%
fast"). Reuse the existing `in_checkin` guard and the pinned-clock test
seam (`ehem_auth_test_set_clock`) to fabricate drifted challenges in
unit tests; live drift cannot be fabricated remotely, so the live leg is
opportunistic (assert login green; exercise recovery only if drift
happens to be present). Wrong-passphrase 401s must NOT check-in (drift
gate). Watch composition budgets: ≤ 1 check-in per ensure_token across
this + the 403-RTC-unset + expired-cert paths (transport-call-count
asserts). Options append-only; zero-init = old behavior.

**Definition of done**
- [x] `checkin_on_login` in ehem_options (append-only, documented);
      recovery + option code tagged `implements: REQ-AUTH-004` /
      `REQ-AUTH-005`
- [x] Unit tests green (gcc+clang+asan): drifted-401 → one checkin + one
      retry → success; second 401 → AUTH_FAILED, no second checkin;
      un-drifted 401 → zero checkins; no_auto_checkin honored;
      checkin_on_login fires once per context, failure non-fatal;
      composition call-counts
- [x] Live: fresh-context login green on my.ence.do with and without
      checkin_on_login; recovery exercised opportunistically if drift
      present (recorded either way — drift was ~24 min < TTL today, so
      logins succeeded without the 401 path; clock-resync healing observed
      via the reordered live suite)
- [x] Export/header/ABI gates green; MinGW cross-syntax check run
