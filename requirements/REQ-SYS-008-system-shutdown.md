---
id: REQ-SYS-008
title: Binding for /api/system/shutdown — stop network and USB services
status: verified
priority: must
revision: 2
source: ARCHITECTURE.md §11 (M7: system group), §1 (dangerous operations API-complete but test-gated); encedo-hem-api-doc system/shutdown.md; encedo_firmware api_system.c:2230 api_get_system_shutdown (fw v1.2.2); approved 2026-07-17; rev bump 2026-08-06 = attended M9-060 gate run resolves the shutdown criterion
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-SYS-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#9-testing-policy"]
---

# Binding for /api/system/shutdown — stop network and USB services

The SDK SHALL provide `ehem_system_shutdown(ctx)` over
`GET /api/system/shutdown`, treating the empty-200-then-close response
as success and dropping the context's entire token cache (as
REQ-SYS-005's reboot does).

- **Recovery requires a physical power-cycle** — the firmware sends
  `200 | close`, waits 2 s, calls `udc_stop()` and deletes the
  web-server task; there is no wake-up endpoint. The binding's docs
  carry this warning verbatim.
- Scope: the device accepts `system:shutdown` OR `system:config`; the
  SDK requests the narrower **`system:shutdown`**.
- Same empty-2xx success convention as reboot: the shared path's
  empty-body 2xx (PROTOCOL + http_status 200) is success; connection
  drop alone is not.
- Errors: 409 (install in progress) → `EHEM_ERR_DEVICE`; 403 →
  `EHEM_ERR_SCOPE_DENIED`.
- **Test policy (stricter than `disruptive`):** the live leg makes the
  device unreachable until someone physically power-cycles it, so it is
  NEVER exercised by the disruptive suite unattended; verification is
  unit tests plus an optional attended manual demo. The step records
  this under `evidence.notes` per §5.2.

**Rationale:** completes the system group per M7; ships API-complete but
test-gated per the fixed architecture decision (§1) — same rationale as
reboot, with an even stronger gate because recovery is physical.

**Acceptance criteria:**
- [x] Unit (fake transport): GET to the right path with the
      `system:shutdown` scope; empty-200 → `EHEM_OK` AND the whole token
      cache invalidated (next authed call re-logins — call-count
      asserted); 409/403 mapping.
- [x] Export/header gates green; binding documented with the
      physical-recovery warning.
- [x] ~~OPEN~~ **RESOLVED (attended, M9-060 gate, 2026-08-06, user
      decision to run it):** scripted `ehem_system_shutdown()` after a
      passphrase login returned EHEM_OK (the empty-200 accepted-shape),
      and the device went DARK — status probes UNREACHABLE from ~3 s
      after the call. Run as the deliberately-LAST live act of the 1.0
      gate (after the 23/23 sweep and the KEY-008 probe); recovery =
      the user's physical power-cycle, as designed. Never entered the
      disruptive suite — attended-manual policy holds.
