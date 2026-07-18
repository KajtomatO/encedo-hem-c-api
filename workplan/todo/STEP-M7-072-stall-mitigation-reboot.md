---
id: STEP-M7-072
title: "Stall mitigation: per-test reboot flag + hem-tool reboot"
milestone: M7
implements: ["REQ-TEST-005", "REQ-TOOL-014"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `EHEM_TEST_REBOOT_EACH=1` reboots + waits at test startup; unset →
      identical behavior to before (flag documented in dev help)
- [ ] `./dev test it|all --reboot-each` sets the flag for the run
- [ ] `hem-tool reboot [--wait]` wired with exit codes 0/1/2 and
      `--help` text
- [ ] Live: single test demonstrated with the flag; full
      `./dev test it --reboot-each` sweep outcome recorded in
      REQ-TEST-005; `hem-tool reboot --wait` demonstrated
- [ ] Unit suite untouched and green; gates green; MinGW cross-syntax
      check on touched TUs
