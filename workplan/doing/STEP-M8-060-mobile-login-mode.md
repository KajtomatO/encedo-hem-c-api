---
id: STEP-M8-060
title: Mobile login mode — ehem_login_mobile + confirm_timeout_ms wired into ensure_token
milestone: M8
implements: ["REQ-AUTH-010"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M8-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_login_mobile(ctx)` (lazy mode switch, mutually exclusive
with passphrase login, scrub-on-switch) and append-only
`ehem_options.confirm_timeout_ms` (0 → 60 000); `ensure_token` cache
misses in mobile mode run the M8-050 blocking flow for the requested
scope, so every existing binding works unchanged and
USER_REJECTED/CONFIRM_TIMEOUT surface from the triggering call.

**Notes:** the ensure_token chokepoint is the ONLY divergence point —
audit its 401-retry interplay (a 401 in mobile mode re-runs the confirm
flow at most once, composing with, not multiplying, the existing retry
budget). Pairing-trio calls in mobile mode fail fast (documented
sub!=U outcome) without firing a push. `checkin_on_login` fires on the
first mobile acquisition as it does for passphrase. Header docs record
the per-KID-scope one-push/15-min consequence.

**Definition of done**
- [ ] Unit: mode switch + scrub (ASan), binding-call-triggered confirm
      flow end-to-end (scripted broker), cache hit = no broker traffic,
      rejected/timeout surfaced from a binding call, timeout default +
      override, 401-retry composition, pairing-trio fail-fast
- [ ] `./dev ci` + asan green; export/header/ABI gates green
      (options append-only); tags placed; evidence filled
