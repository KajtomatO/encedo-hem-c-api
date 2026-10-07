---
id: STEP-M10-010
title: "Wipeout binding: ehem_system_wipeout (device factory reset)"
milestone: M10
implements: ["REQ-SYS-014"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_system_wipeout(ctx)` in `include/ehem/system.h` /
`src/proto_system.c`: authenticated `POST /api/system/config` with body
`{"wipeout":true}` (scope `system:config`); on 200 the context drops its
token cache and retained credential (REQ-AUTH-002 logout semantics).
Unit-tested against the fake transport; documented in the header and in
`docs/API-GUIDE.md` (the `docs_coverage` gate fails on any undocumented
public symbol).

**Notes:** Reuse the config-POST path of `ehem_system_config_install_cert`
and the logout helper of `proto_auth.c` for the cache/credential drop.
The firmware answers 200 BEFORE wiping (2 s delay, then restart) — the
binding returns on the 200; what follows (uninitialised, HTTP-only, RTC
unset, repo gone) is the tool's and the header's story. Firmware demands
`sub` U/M for every config POST (api_system.c:1060-1066) → a mobile
context gets 403 → `EHEM_ERR_SCOPE_DENIED`. **Attended-only
(REQ-TEST-007): add NO live test file** — not in `integration/`, not in
`disruptive/`. The attended run happens at the M10 gate (STEP-M10-070)
and is recorded in REQ-SYS-014.

**Definition of done**
- [ ] Binding + header docs (consequences, pointers to `ehem_device_init`
      and `ehem_tls_recover`); `docs/API-GUIDE.md` index updated.
- [ ] Unit tests (tests/unit/test_config.c): exact body/scope/bearer;
      200 → EHEM_OK + cache and credential dropped (next call needs
      login); 406/403/401 mapped with detail.
- [ ] No live test added (grep `ehem_system_wipeout` under tests/ finds
      only the unit test).
- [ ] `./dev ci` green (gcc + clang), ASan clean, export/header/docs gates
      green; `implements: REQ-SYS-014` tagged.
