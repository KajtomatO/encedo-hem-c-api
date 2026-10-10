---
id: STEP-M10-010
title: "Wipeout binding: ehem_system_wipeout (device factory reset)"
milestone: M10
implements: ["REQ-SYS-014"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-SYS-014 — tests/unit/test_config.c (test_wipeout_200_drops_session: exact body {\"wipeout\":true} POSTed to /api/system/config with a bearer, empty-200 → EHEM_OK, then ehem_auth_ensure_token → AUTH_EXPIRED with zero traffic; test_wipeout_406_session_intact: → EHEM_ERR_DEVICE, status 406, token reused; test_wipeout_403: → SCOPE_DENIED, session intact)"
  notes: >
    2026-10-07. ehem_system_wipeout() added to include/ehem/system.h +
    src/proto_system.c (implements: REQ-SYS-014): literal body
    {"wipeout":true} through ehem_proto_request_raw (scope system:config);
    the device's empty 200 (sent BEFORE the 2 s wipe) is accepted the way
    reboot accepts it (PROTOCOL/200 → OK); success calls ehem_logout()
    — token cache AND retained credential dropped, so the next
    authenticated call fails AUTH_EXPIRED without network. Header doc
    spells out the consequences (uninitialised, HTTP-only, RTC unset,
    repository gone) and points at ehem_device_init / ehem_tls_recover /
    ehem_system_checkin. docs/API-GUIDE.md system index row added
    (docs_coverage gate green). tests/README.md gained the attended-only
    row (REQ-TEST-007). NO live test added (grep: ehem_system_wipeout
    appears under tests/ only in tests/unit/test_config.c and the README).
    Verified: ./dev ci 41/41 on gcc AND clang; ./dev test asan clean;
    ./dev check (export baseline + public-header + docs gates) green;
    x86_64-w64-mingw32-gcc -fsyntax-only on proto_system.c clean (the
    MinGW printf-attribute trap). Attended run of the binding = the M10
    gate (STEP-M10-070); until then REQ-SYS-014 stays `implemented`.
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
- [x] Binding + header docs (consequences, pointers to `ehem_device_init`
      and `ehem_tls_recover`); `docs/API-GUIDE.md` index updated.
- [x] Unit tests (tests/unit/test_config.c): exact body/scope/bearer;
      200 → EHEM_OK + cache and credential dropped (next call needs
      login); 406/403 mapped with detail (401 shares the common path's
      single re-acquire + AUTH_FAILED mapping, already unit-pinned in
      test_auth.c).
- [x] No live test added (grep `ehem_system_wipeout` under tests/ finds
      only the unit test and the README row).
- [x] `./dev ci` green (gcc + clang), ASan clean, export/header/docs gates
      green; `implements: REQ-SYS-014` tagged.
