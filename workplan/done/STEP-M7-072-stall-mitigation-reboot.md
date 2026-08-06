---
id: STEP-M7-072
title: "Stall mitigation: per-test reboot flag + hem-tool reboot"
milestone: M7
implements: ["REQ-TEST-005", "REQ-TOOL-014"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits: ["a713910"]
  tests: ["verifies: (live) REQ-TEST-005 single-test + full-sweep runs, REQ-TOOL-014 reboot --wait demo — 2026-07-18; test-infra REQ, no unit test (the harness change is exercised by every gated live run)"]
  notes: >
    EHEM_TEST_REBOOT_EACH=1 hook in ehem_require_test_url()
    (integration_env.h, supports: REQ-TEST-005) + `./dev test it|all
    --reboot-each` + `hem-tool reboot [--wait]` (REQ-TOOL-014). Wait
    design: two phases (must SEE the device down first — the firmware
    serves ~2 s after accepting a reboot and fooled v1 into a false ~0 s
    return), probes spaced by the SDK's own request_pace_ms (the one
    portable "sleep" available to a strict-C99 shared header), bounded
    ~180 s (returns observed 12-87 s). Fail-fast on no-return so a
    wedged device fails the suite in seconds. LIVE: reboot --wait 13 s
    green; single test rebooted+passed; no-passphrase note path green.
    FULL SWEEP OUTCOME (recorded in REQ-TEST-005 rev2): 8 tests
    rebooted+passed, then the sweep failed anyway — first repo-level
    406s (fixed import constants deduped ACROSS delete+reboot →
    REQ-KEY-008 rev3 open criterion; then ehem_key_create 406ing), then
    the device wedged (physical power-cycle needed). NEW HYPOTHESIS for
    the stall: key-repo debris (repo_stats: 1554+ deleted slots,
    fragmented 99) rather than pure request load. Hardened
    test_update_import_live: per-run-unique import material +
    dedup-tolerant type probe. Unit 31/31 gcc+clang+ASan; gates green;
    MinGW cross-syntax OK; shellcheck clean.
reopened: []
cancelled: null
---

**Goal:** the integration suite can opt into a device reboot before each
test binary (`EHEM_TEST_REBOOT_EACH=1` / `./dev test it --reboot-each`,
REQ-TEST-005) so the full sweep survives the firmware's
stall-under-sustained-load mode; `hem-tool reboot [--wait]` exposes the
REQ-SYS-005 binding on the CLI (REQ-TOOL-014).

**Notes:** Mid-milestone insertion (user decision 2026-07-18) after the
2026-07-18 full sweep stalled the device around test_pqc_live — every
test had passed individually. The reboot hook lives in
`ehem_require_test_url()` (tests/support/integration_env.h) so all 21+
integration mains inherit it; bounded post-reboot status polling
(~90 s), portable sleep (nanosleep/Sleep). Off by default; needs the
passphrase (note + skip without it). CI never sets the flag.

**Definition of done**
- [x] `EHEM_TEST_REBOOT_EACH=1` reboots + waits at test startup; unset →
      identical behavior to before (flag documented in dev help)
- [x] `./dev test it|all --reboot-each` sets the flag for the run
- [x] `hem-tool reboot [--wait]` wired with exit codes 0/1/2 and
      `--help` text
- [x] Live: single test demonstrated with the flag; full
      `./dev test it --reboot-each` sweep outcome recorded in
      REQ-TEST-005 rev2 (mechanism works; the stall's real driver looks
      like repo debris — mitigation alone insufficient);
      `hem-tool reboot --wait` demonstrated (13 s)
- [x] Unit suite untouched and green; gates green; MinGW cross-syntax
      check on touched TUs
