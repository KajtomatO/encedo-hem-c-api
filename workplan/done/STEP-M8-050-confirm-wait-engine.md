---
id: STEP-M8-050
title: Mobile confirm engine — pollable begin/poll/cancel + blocking wait with timeout
milestone: M8
implements: ["REQ-AUTH-009"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M8-040"]
evidence:
  commits: ["82ee772"]
  tests: ["verifies: REQ-AUTH-009 — tests/unit/test_confirm.c (4 cases: begin wire shape; pending→approved seeds the cache under the requested scope and a subsequent ehem_system_config call sends the seeded bearer with zero acquisitions; deny → USER_REJECTED terminal with no /ext/token; wait timeout after exactly the computed polls, handle resumable to approval, sub-interval timeout still polls; transient 500 retryable; terminal misuse ARG; ASan leak-free)"]
  notes: "Public ehem_ext_confirm_begin/poll/wait/cancel in auth.h; engine in proto_ext.c. Supporting internals: ehem_auth_cache_seed (proto_auth — exp-claim expiry, creates auth state for credential-less contexts), ehem_proto_sleep_ms (proto_common — the pace sleep exposed internally), src/proto_ext.h poll-interval test seam (default 5 s, the tester cadence). Poll never sleeps; wait clamps the final sleep to the remaining budget; timeout is NOT terminal (resumable); approved/denied/redeem-failure are. Unit 36/36 gcc+clang+ASan; export/header gates green; MinGW cross-syntax clean (proto_ext.c, proto_common.c). Live approve/reject/timeout = attended M8-080 by design."
reopened: []
cancelled: null
---

**Goal:** the push-confirm acquisition state machine: `begin` (broker
session GET → `ehem_ext_request` → `event/new`; opaque single-use
handle), `poll` (one `event/check`; pending / approved→`ehem_ext_token`→
token-cache seed under the requested scope / `deny`→
`EHEM_ERR_USER_REJECTED`), `cancel` (free, no network), and the blocking
`wait(timeout_ms)` → `EHEM_ERR_CONFIRM_TIMEOUT` on deadline. Every leg
credential-free.

**Notes:** poll interval default 5 s, injectable via a hidden test seam
(the `ehem_auth_test_set_clock` pattern) so unit tests never sleep; a
timeout shorter than one interval still polls once. Cache seeding uses
the caller's PRE-rewrite scope string as the key and the bearer's own
`exp` claim (existing decode path). Public API naming/placement in
auth.h decided here — keep the reserved-pollable-variant wording of §5
in the header docs. Unit-only step: live approve/reject/timeout is
attended (M8-080); the scripted-fake broker covers every terminal.

**Definition of done**
- [x] Unit: approved seeds cache (subsequent binding call = zero
      acquisitions), deny terminal without `/ext/token`, perpetual-202
      timeout, cancel ASan-clean in every state, interval seam honored
- [x] Unit: begin/poll/cancel misuse (double-finish, poll-after-
      terminal) fails `EHEM_ERR_ARG`, no UB (ASan)
- [x] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
