---
id: REQ-SYS-014
title: Binding for the wipeout write of /api/system/config — device factory reset
status: approved
priority: should
revision: 1
source: user decision 2026-10-07 (M10 scope: device wipe); ARCHITECTURE.md §11 (M10); encedo-hem-api-doc system/config.md (`wipeout` field; `false` = 406 no-op sentinel); encedo_firmware api_system.c:1060-1066 (POST config demands scope `system:config` AND token `sub` U or M) and :1089-1099 (200 is sent first, then 2 s delay, `system_full_wipeout()`, bootloader restart); REQ-SYS-004 (the config binding this extends); approved 2026-10-07 (M10 decomposition, user go-ahead)
depends_on: ["REQ-SYS-004", "REQ-AUTH-002", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
---

# Binding for the wipeout write of /api/system/config — device factory reset

The SDK SHALL provide `ehem_system_wipeout(ctx)` — authenticated
`POST /api/system/config` with the body `{"wipeout": true}` (scope
`system:config`) — and, on a 200 reply, SHALL drop the context's token
cache and retained credential (REQ-AUTH-002 logout semantics), because
nothing the context holds is valid against the device afterwards.

Device facts (fw v1.2.2, `api_system.c`):
- The handler checks scope `system:config` **and** token `sub` ∈ {`U`,
  `M`} (`:1060-1066`) → a mobile-acquired bearer (`sub` = base64(kid),
  REQ-AUTH-007) is rejected with 403. The binding therefore needs a
  passphrase session; `ehem_login_mobile` contexts fail with the mapped
  `EHEM_ERR_SCOPE_DENIED`.
- `wipeout: true`: the device answers **200 before acting** (`:1094`),
  waits 2 s, erases the configuration (`system_full_wipeout()`) and
  restarts through the bootloader. `wipeout: false` → 406 (no-op
  sentinel). Other 4xx/5xx map per REQ-API-003.
- After the wipe the device is **uninitialised** (`GET /api/auth/init`
  serves a challenge; token endpoints refuse), its TLS key and
  certificate are gone (HTTP-only — REQ-SYS-013 territory), the RTC is
  unset after the restart (check-in needed, REQ-SYS-003) and the key
  repository is unreadable (its key lived in the erased configuration).

**Rationale:** M10 makes the SDK able to initialise a wiped device
(REQ-AUTH-011); re-initialisation requires a wipeout first
(`auth/init.md`), and the only alternative is a manual wipe through the
Manager each time. The binding is deliberately plain: no confirmation
logic in the library — that is the tool's job (REQ-TOOL-022).

**Acceptance criteria:**
- [ ] Unit (fake transport): the body is exactly `{"wipeout":true}`,
      scope `system:config`, bearer present; 200 → `EHEM_OK` and the
      token cache + retained credential are dropped (a following
      authenticated call needs `ehem_login` again); 406, 403, 401 map
      per REQ-API-003 with `ehem_last_error` detail.
- [ ] **Attended-only** (REQ-TEST-007): no live CTest exists for this
      binding, not even `disruptive`-gated. Evidence to record here when
      performed: date, device, observed sequence (200 → device down →
      back uninitialised: `GET /api/auth/init` returns a challenge).
- [ ] The public header documents the consequences (uninitialised,
      HTTP-only, RTC unset, repository gone) and points at
      `ehem_device_init` (REQ-AUTH-011) and `ehem_tls_recover`
      (REQ-SYS-013).
