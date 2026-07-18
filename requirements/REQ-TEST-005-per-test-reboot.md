---
id: REQ-TEST-005
title: Opt-in device reboot before each integration test (stall mitigation)
status: approved
priority: should
revision: 1
source: user decision 2026-07-18 ("the full sweep fails around test_pqc_live — as a temporary measure, add a reboot for every IT test, enabled by some flag"); KNOWN-ISSUES.md "Device stalls/hangs under sustained load" (watchdog firmware-disabled)
depends_on: ["REQ-SYS-005", "REQ-TEST-002", "REQ-NET-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
---

# Opt-in device reboot before each integration test (stall mitigation)

When the environment variable `EHEM_TEST_REBOOT_EACH` is `1`, every
integration-test executable SHALL reboot the device once at startup (via
`ehem_system_reboot`) and wait for it to serve requests again before its
cases run.

- **Temporary mitigation, off by default:** the dev HEM stalls under
  sustained load (KNOWN-ISSUES; the watchdog is firmware-disabled, so a
  stall needs a physical power-cycle). Rebooting between test binaries
  resets the firmware's resource state so the full suite can cross the
  stall threshold. Request pacing (REQ-NET-006) remains the first line;
  this flag is the heavier second line until the firmware is fixed.
- Implemented in the shared test-support startup path
  (`ehem_require_test_url`), so every current and future integration test
  inherits it without per-test wiring; the disruptive suite shares the
  same startup and therefore the same behavior.
- The reboot leg needs `EHEM_TEST_PASSPHRASE`; with the flag set but no
  passphrase the test proceeds without rebooting (a stderr note says so).
  A failed reboot or a device that never comes back within the bounded
  wait (~90 s of status polling) fails fast with a clear message rather
  than letting the suite run against a wedged device.
- `./dev test it --reboot-each` (and `test all --reboot-each`) sets the
  variable for the run — the discoverable spelling of the flag.
- Rebooting is a disruptive-class side effect: the flag is an explicit
  opt-in exactly like `EHEM_ALLOW_DISRUPTIVE`, never set by default, and
  never set in CI.

**Rationale:** the 2026-07-18 full sweep (21 integration tests, ~450
requests after a day of probing) stalled the device around test_pqc_live
— every test had passed individually at step level. Per-binary reboots
trade wall-clock time (~30-60 s per test) for a bounded firmware state,
which is the right trade for an attended verification run.

**Acceptance criteria:**
- [ ] With the flag unset, no integration test issues a reboot (the
      default path is byte-identical to before).
- [ ] With `EHEM_TEST_REBOOT_EACH=1` and credentials set, a single
      integration test visibly reboots the device, waits until status
      answers, then passes its cases (live demonstration).
- [ ] Full `./dev test it --reboot-each` sweep against the real device
      completes without the stall (the motivating scenario) — or the
      outcome is recorded here if the stall persists even with reboots.
- [ ] Flag set but no passphrase → stderr note, no reboot, tests proceed.
