---
id: STEP-M8-050
title: Mobile confirm engine — pollable begin/poll/cancel + blocking wait with timeout
milestone: M8
implements: ["REQ-AUTH-009"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M8-040"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Unit: approved seeds cache (subsequent binding call = zero
      acquisitions), deny terminal without `/ext/token`, perpetual-202
      timeout, cancel ASan-clean in every state, interval seam honored
- [ ] Unit: begin/poll/cancel misuse (double-finish, poll-after-
      terminal) fails `EHEM_ERR_ARG`, no UB (ASan)
- [ ] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
