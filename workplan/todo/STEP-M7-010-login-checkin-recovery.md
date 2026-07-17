---
id: STEP-M7-010
title: "Clock-drift login recovery + checkin_on_login option"
milestone: M7
implements: ["REQ-AUTH-004", "REQ-AUTH-005"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `checkin_on_login` in ehem_options (append-only, documented);
      recovery + option code tagged `implements: REQ-AUTH-004` /
      `REQ-AUTH-005`
- [ ] Unit tests green (gcc+clang+asan): drifted-401 → one checkin + one
      retry → success; second 401 → AUTH_FAILED, no second checkin;
      un-drifted 401 → zero checkins; no_auto_checkin honored;
      checkin_on_login fires once per context, failure non-fatal;
      composition call-counts
- [ ] Live: fresh-context login green on my.ence.do with and without
      checkin_on_login; recovery exercised opportunistically if drift
      present (recorded either way)
- [ ] Export/header/ABI gates green; MinGW cross-syntax check run
