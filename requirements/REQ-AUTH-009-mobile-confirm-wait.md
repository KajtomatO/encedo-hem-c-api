---
id: REQ-AUTH-009
title: Mobile confirmation wait — pollable state machine with blocking wrapper, distinct rejected/timeout results
status: verified
priority: must
revision: 3
source: user decision 2026-07-22 (M8 decomposition); ARCHITECTURE.md §5 mobile-app confirmation bullet; start_point HEM-SDK-8/HEM-AUTH-2 (distinct rejection vs timeout, blocking with confirm_timeout); rev bump 2026-08-06 = STEP-M9-055 asymmetric drift gate (user decision: threshold only)
depends_on: ["REQ-AUTH-002", "REQ-AUTH-007", "REQ-AUTH-008"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
---

# Mobile confirmation wait — pollable state machine with blocking wrapper, distinct rejected/timeout results

The SDK SHALL provide a push-confirm acquisition engine: a non-blocking
pollable primitive (`begin` / `poll` / `cancel`) over the
request→push→check→token sequence, plus a blocking wrapper with a
caller-supplied timeout, yielding `EHEM_ERR_USER_REJECTED` when the user
denies on the phone and `EHEM_ERR_CONFIRM_TIMEOUT` when the deadline
passes unanswered.

- **begin(ctx, scope, ctx_str, note, &confirm):** broker `session` (the
  credential-free GET form — mobile mode holds no passphrase and needs
  no eid; the device's identity arrives in the authreq it signs) →
  `ehem_ext_request` → broker `event/new`; returns an opaque in-progress
  handle owning the `eventid`. One device round-trip + two cloud legs;
  no waiting. Every leg is unauthenticated by construction.
  **Drift recovery (rev 2, found live at M8-080, 2026-08-05):** the broker rejects
  an authreq whose `iat` is in its future (REQ-AUTH-008 rev 3) and the
  device RTC runs ~8% fast, so `begin` SHALL, on an `event/new` HTTP 401
  WITH drift evidence (rev 3, STEP-M9-055: authreq `iat` AHEAD of
  local now by > 2 s — the broker has ~zero tolerance for a future iat,
  live-observed rejecting +14 s on 2026-08-06, so the rev-2 15 s gate
  left a (0,15] s dead window re-entered ~3 min after every resync at
  the ~8%-fast RTC; or `iat` BEHIND by > 15 s, a wrong-local-clock
  reading), run ONE
  check-in (the firmware resyncs its RTC) and re-fire all three legs
  once — a fresh authreq is mandatory, the old `iat` stays bad. Honors
  `no_auto_checkin` and the check-in recursion guard; without drift
  evidence a 401 is a real broker refusal and stands. The
  REQ-AUTH-004 pattern, keyed on the broker's clock check.
- **poll(ctx, confirm):** ONE broker `event/check`:
  - 202 → returns "still pending" (a non-error status out-param — poll
    itself never sleeps and applies no deadline; cadence and deadline
    belong to the caller or the blocking wrapper);
  - 200+`authreply` → `ehem_ext_token`, seed the REQ-AUTH-002 token
    cache under the REQUESTED scope (the pre-rewrite string the caller
    passed — that is the cache key bindings look up; entry expiry from
    the bearer's own `exp` claim as usual), return terminal success;
  - 200+`deny` → terminal `EHEM_ERR_USER_REJECTED`;
  - transport/broker/device errors → their normal mapping, terminal.
- **cancel(ctx, confirm):** frees the handle; no broker/device call (the
  event simply expires server-side with the authreq `exp`). Safe after
  terminal poll; `confirm` is single-use.
- **Blocking wrapper `wait(ctx, confirm, timeout_ms)`:** polls at a
  bounded default interval (5 s, the tester's cadence; interval capped
  so short timeouts still poll at least once) until terminal or
  `timeout_ms` elapses → `EHEM_ERR_CONFIRM_TIMEOUT`. Timeout does NOT
  invalidate the handle's memory (caller still frees via cancel), and a
  push answered on the phone after the SDK gave up has no effect — no
  `/ext/token` call ever happens.
- The two mobile-specific errors are already in the ABI enum
  (`ehem.h`, reserved since M1) — no enum change. HEM-SDK-8's
  distinguishable-conditions contract is the driver: PKCS#11 maps
  rejected AND timeout to `CKR_FUNCTION_CANCELED` but logs them apart;
  other consumers may branch.

**Rationale:** ARCHITECTURE §5 commits to "blocking wait with
caller-supplied timeout; the API reserves a pollable variant for
consumers that cannot block" — implementing blocking ON TOP of the
pollable machine gives both for one state machine and lets unit tests
drive every terminal through a scripted fake broker with zero sleeping
(pace/interval injectable via a hidden test seam, the
`ehem_auth_test_set_clock` precedent).

**Acceptance criteria:**
- [x] Unit (tests/unit/test_confirm.c, 2026-07-23, STEP-M8-050 +
      the rev-2 drift-recovery case at M8-080): pending→approved seeds the
      cache under the requested scope and a subsequent binding call uses
      the bearer with NO new acquisition; pending→deny →
      `EHEM_ERR_USER_REJECTED` (terminal, no `/ext/token` call);
      perpetual-202 + wrapper deadline → `EHEM_ERR_CONFIRM_TIMEOUT`;
      cancel leaks nothing (ASan) in every state.
- [x] Unit: wrapper poll cadence honors the interval seam; a timeout
      shorter than one interval still performs ≥1 poll (2026-07-23).
- [x] Live (attended, real phone, STEP-M8-080, 2026-08-05): approved →
      EHEM_OK with a working bearer (config data printed); rejected →
      `EHEM_ERR_USER_REJECTED`; unanswered → `EHEM_ERR_CONFIRM_TIMEOUT`
      — all three BACK-TO-BACK, which also exercises the rev-2 drift
      recovery live (the pre-fix run reproduced the broker 401 within
      minutes of a clock sync).
