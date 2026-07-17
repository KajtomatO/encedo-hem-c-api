---
id: REQ-AUTH-005
title: Opt-in proactive check-in at first token acquisition (checkin_on_login)
status: approved
priority: should
revision: 1
source: user decision 2026-07-17 (M7 decomposition; user note 2026-07-17 recorded in KNOWN-ISSUES.md clock-drift entry — "encedo-pkcs11 should perhaps always run a check-in when a new session starts; maybe that belongs in the SDK as an option")
depends_on: ["REQ-AUTH-001", "REQ-SYS-003", "REQ-AUTH-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions"]
---

# Opt-in proactive check-in at first token acquisition (checkin_on_login)

When the new append-only option `ehem_options.checkin_on_login` is set,
the session engine SHALL run one best-effort check-in before the first
token acquisition on the context, and none thereafter.

- **Once per context:** the check-in runs at the first
  `ehem_auth_ensure_token` network flow (lazy login means `ehem_login`
  itself stays offline), then never again for the context's lifetime —
  matching "a new session starts" in the consumer use case.
- **Best-effort:** a check-in failure (cloud relay unreachable, device
  legs failing) is recorded in last-error detail but does NOT fail the
  login; the flow proceeds and REQ-AUTH-004 remains the backstop.
- Default off; `no_auto_checkin` does not affect it (the option is an
  explicit request, not an automatic recovery).
- ABI: options struct grows append-only, zero-init keeps old behavior
  (same discipline as `no_credential_retention`, `request_pace_ms`).

**Rationale:** heals both known device pathologies (RTC drift ~8% fast,
cert rotation) with one unauthenticated round-trip at a predictable
moment — exactly what a PKCS#11 session-open wants; the reactive
REQ-AUTH-004 alone still costs one failed login round-trip when drift has
accumulated. Kept separate from REQ-AUTH-004 per the one-behavior rule.

**Acceptance criteria:**
- [ ] Unit (fake transport): option set → exactly one check-in before the
      first challenge GET, none on later token acquisitions (call-count
      asserted); option clear → zero check-ins.
- [ ] Unit: check-in failure with the option set → login still proceeds
      and succeeds; failure detail retrievable via `ehem_last_error`.
- [ ] Live: with `checkin_on_login` set, first authed call on a fresh
      context performs checkin + login + request against my.ence.do and
      succeeds; device clock verified resynced (status `ts` sane).
- [ ] ABI: existing zero-initialized `ehem_options` users compile and run
      unchanged (append-only check in test_context).
