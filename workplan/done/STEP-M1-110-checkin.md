---
id: STEP-M1-110
title: Check-in binding + automatic TLS-certificate recovery
milestone: M1
implements: ["REQ-SYS-003", "REQ-NET-005", "REQ-TOOL-002"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M1-060", "STEP-M1-070", "STEP-M1-090", "STEP-M1-100"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-SYS-003, REQ-NET-005 (tests/unit/test_checkin.c — 9 cases; tests/integration/test_checkin_live.c live)", "verifies: REQ-TOOL-002 (manual smoke, see notes — main() has no unit test)"]
  notes: >
    Binding: ehem_system_checkin()/ehem_checkin_result_free() +
    ehem_checkin_info (all-optional strings, cert_updated derived from
    non-empty newcrt) in include/ehem/system.h; the 3-leg flow lives in
    proto_system.c (ehem_checkin_run, shared with the auto-recovery hook):
    GET /api/system/checkin → POST the challenge VERBATIM to
    ctx->checkin_url (default EHEM_DEFAULT_CHECKIN_URL
    https://api.encedo.com/checkin, options-overridable; Content-Type:
    application/json) → POST the cloud reply VERBATIM back to the device.
    TLS posture per REQ-SYS-003: cloud leg forced EHEM_TLS_REQ_VERIFY (even in
    an INSECURE context — and in CA_FILE mode the pinned device CA is swapped
    out for the system bundle on that leg); device legs EHEM_TLS_REQ_RELAX
    (payloads are cloud-signed, device-validated). Transport grew a
    per-request tls_override + fresh_connection in ehem_request, absolute-URL
    passthrough in join_url, and expired-cert classification
    (CURLE_PEER_FAILED_VERIFICATION + CURLINFO_SSL_VERIFYRESULT==10, errbuf
    "expired" fallback) exposed via the optional last_tls_expired op.
    Auto-recovery (REQ-NET-005) sits in the shared request path
    (src/proto_common.c, extracted from proto_system.c so M2+ bindings inherit
    it): expired-only trigger, no_auto_checkin opt-out (default ON),
    in_checkin recursion guard, single fresh-connection retry;
    ehem_cert_refreshed(ctx) true only when the refresh took EFFECT (retry
    verified). hem-tool: stderr notice after status when refreshed +
    `checkin` subcommand. ARCHITECTURE.md §7 retry sentence and §11 M1 line
    amended. Verified on Linux (2026-07-15): `ctest -L unit` 9/9 green (GCC
    and Clang; test_checkin covers happy flow with sequence/override/verbatim
    asserts, custom cloud URL, auto-recovery, opt-out, non-expired
    no-trigger, recovery-failure reporting, device-still-serves-old-cert,
    recursion guard, NULL-safe free), GCC ASan/LSan clean, exports still only
    ehem_*, integration gating intact (11/11 with both live tests Skipped
    when EHEM_TEST_URL unset).
    LIVE (dev HEM https://my.ence.do + api.encedo.com, 2026-07-15):
    `ctest -L integration` 2/2 green (status/version + checkin round-trips);
    `hem-tool checkin` → "status: OK / cert refreshed: yes";
    `hem-tool status` under system trust drove the FULL auto-recovery against
    the genuinely expired cert — check-in succeeded, device accepted the
    update, but kept serving the old cert (reboot-to-apply, or the cloud
    re-delivers a stale cert; recorded in REQ-SYS-003/REQ-NET-005), so the
    tool reported the original error with the explanatory
    "...still serves the old certificate — a device reboot may be required"
    detail and exit 1. The happy retry path (device applies immediately) is
    unit-covered; live re-check after the next device reboot.
reopened: []
cancelled: null
---

**Goal:** `ehem_system_checkin()` implementing the three-leg check-in
handshake (device challenge → Encedo cloud verify → device apply), automatic
recovery when a request fails on an expired device certificate (check-in +
single retry, opt-out via options, queryable via `ehem_cert_refreshed`), and
hem-tool surfacing the refresh (stderr notice + `checkin` subcommand).

**Notes:** Added to M1 post-gate (user decision 2026-07-15) after the gate
found the dev device's certificate expired. Trigger scope is expired-cert
only (security: broader auto-relaxation would hand a MITM the relaxed path).
Cloud leg (`https://api.encedo.com/checkin`, options-overridable) always
verifies TLS; device legs may relax verification only inside the flow. The
request struct gains a per-request TLS override; the shared request helper
moves to `proto_common.c` so M2+ bindings inherit the recovery hook.

**Definition of done**
- [x] 3-leg flow with verbatim body relay; leg-2 TLS forced verified, legs 1/3 relaxed only in-flow (unit tests assert sequence + overrides) — *test_checkin_happy_flow, test_checkin_custom_cloud_url*
- [x] Auto-recovery: expired-cert failure → check-in → single fresh-connection retry; expired-only classification; `no_auto_checkin` opt-out; recursion guard; original error preserved on recovery failure (unit tests) — *test_auto_recovery, test_auto_recovery_optout, test_non_expired_failure_no_recovery, test_recovery_failure_reports_original, test_recursion_guard*
- [x] `ehem_cert_refreshed(ctx)` reports a performed refresh; `ehem_checkin_result` + NULL-safe free; ASan/LSan clean — *effective-refresh semantics (test_device_still_serves_old_cert), test_result_free_null_safe; ASan/LSan 9/9*
- [x] hem-tool: stderr notice after operations that refreshed the cert; `checkin` subcommand prints result, nonzero + detail on failure — *print_cert_notice + cmd_checkin; live smoke in evidence*
- [x] Live: recovery and/or explicit check-in demonstrated against the dev-machine HEM; findings recorded in REQ-SYS-003 / REQ-NET-005 — *full recovery driven live; device accepts-but-does-not-apply finding recorded in both REQs*
