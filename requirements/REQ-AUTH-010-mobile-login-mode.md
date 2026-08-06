---
id: REQ-AUTH-010
title: Mobile login mode — ehem_login_mobile and confirm_timeout_ms option
status: verified
priority: must
revision: 3
source: user decision 2026-07-22 (M8 decomposition); start_point REQUIREMENTS-hem.md HEM-SDK-2 (mobile base auth), HEM-AUTH-2 (block until confirmation or confirm_timeout), HEM-CFG-3 (confirm_timeout config key); rev bump 2026-08-06 = STEP-M9-057 confirm_notice hook (user request at gate prep)
depends_on: ["REQ-AUTH-001", "REQ-AUTH-002", "REQ-AUTH-009"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
---

# Mobile login mode — ehem_login_mobile and confirm_timeout_ms option

The SDK SHALL provide `ehem_login_mobile(ctx)` as a base-authentication
mode: after it, every token acquisition through the session engine runs
the REQ-AUTH-009 blocking confirm flow instead of the passphrase eJWT
flow, bounded by a new `ehem_options.confirm_timeout_ms`.

- **`ehem_login_mobile(ctx)`:** lazy, no network — the exact
  `ehem_login` contract (stores the auth MODE where login stores the
  passphrase); mutually exclusive with a passphrase login on the same
  context (last call wins, prior credential material scrubbed).
  `ehem_logout` clears the mode and the whole token cache as today.
- **`ensure_token(ctx, scope)` in mobile mode:** cache hit → return, as
  today. Miss → REQ-AUTH-009 begin/wait/cancel for exactly the requested
  scope with `confirm_timeout_ms`; the acquired bearer lands in the
  same scope-keyed cache, so the 401-retry and every existing binding
  work unchanged. `EHEM_ERR_USER_REJECTED` / `EHEM_ERR_CONFIRM_TIMEOUT`
  surface from whatever binding triggered acquisition (HEM-AUTH-2: same
  UX as a pinpad reader).
- **`confirm_timeout_ms`:** append-only `ehem_options` field; 0 → default
  60 000 (HEM-CFG-3's example `confirm_timeout = 60`). Applies per
  acquisition, not per API call.
- **Documented consequences (not smoothed over):**
  - Per-KID scopes (`keymgmt:use:<kid>`) mean ONE PUSH PER KID and a
    15-minute bearer (REQ-AUTH-007 rewrite) — a `sign` on a fresh KID in
    mobile mode blocks on the phone. Consumers wanting fewer pushes
    request broader scopes (the start_point "session scope" idea is the
    consumer's choice; the SDK stays scope-exact).
  - The bearer's `sub` is the authenticator kid, not `U` — endpoints
    demanding `sub=="U"` (the pairing trio, REQ-AUTH-006) will 403 under
    a mobile-acquired token. Pairing management therefore requires a
    passphrase login; recorded in the header docs.
  - `checkin_on_login` (REQ-AUTH-005) applies to the first mobile
    acquisition too — the flow needs the RTC set just like the
    challenge GET (REQ-AUTH-007's 403 recovery is the reactive
    complement).

**Rationale:** HEM-SDK-2 makes mobile a BASE auth mode, not a bolt-on:
encedo-pkcs11's `C_Login(NULL_PTR)` fires pushes on demand per scope.
Routing it through the existing `ensure_token` chokepoint (the
REQ-AUTH-003 design) means zero changes in the twenty-odd existing
bindings and one place where the two auth modes diverge.

**Acceptance criteria:**
- [x] Unit (tests/unit/test_mobile.c, 2026-07-23, STEP-M8-060): after
      `ehem_login_mobile`, a binding call on a cache miss runs the
      confirm flow and succeeds end-to-end; cache hit performs no
      broker traffic; rejected/timeout surface from the binding call;
      passphrase↔mobile switching scrubs the losing mode's material
      (ASan); `confirm_timeout_ms` default and override honored.
- [x] Unit: pairing-trio call in mobile mode fails fast with the
      documented sub!=U outcome (no push fired for an endpoint that
      cannot accept the resulting token).
- [x] Live (attended, real phone, STEP-M8-080, 2026-08-05):
      `ehem_login_mobile` + `ehem_system_config` (via `hem-tool ext
      login`) → push approved → config data returned (hostname
      my.ence.do, user Debug HSM); deny → `EHEM_ERR_USER_REJECTED`
      (exit 4); unanswered → timeout (exit 3).

**STEP-M9-057 addition (2026-08-06, user request):** append-only
`ehem_options.confirm_notice(scope, timeout_ms, arg)` +
`confirm_notice_arg` — the confirm engine invokes it once per DELIVERED
push (after the broker accepted the confirmation request, including
after a drift-recovery re-fire; never on a failed begin) with the
REQUESTED scope and the effective wait bound. NULL default = off; the
library never prints — consumers own the UI (hem-tool prints its
`mobile:` line from it). Unit: tests/unit/test_confirm.c
test_confirm_notice_hook.
