---
id: STEP-M5-035
title: "Integration suite reliability — request pacing + ctest stall-retry"
milestone: M5
implements: ["REQ-NET-006"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#9-testing-policy"]
depends_on: []
evidence:
  commits:
    - "fcae25b — request pacing (REQ-NET-006) + ctest stall-retry"
  tests:
    - "test_context (verifies: REQ-NET-006): test_options_request_pace — default 0, value copied to ctx, old-ABI abi_size keeps default. gcc+clang + asan green; export/header gates green"
    - "LIVE 2026-07-17: ./dev test it 11/11 GREEN with EHEM_TEST_PACE_MS=150, device alive throughout (167s, no retries needed); this is the exact full-suite scenario that hard-hung the device twice before the mitigation"
  notes: >
    src/proto_common.c cross-compiles clean for x86_64-w64-mingw32 (new
    Windows Sleep() path). Pacing applied at the do_send chokepoint for every
    dispatch incl. retries; SDK reads no env (harness passes the option).
    dev: integration runs use ctest --repeat until-pass:${EHEM_TEST_REPEAT:-3}
    + EHEM_TEST_PACE_MS default 150; bash -n clean. KNOWN-ISSUES.md updated.
reopened: []
cancelled: null
---

**Goal:** The integration suite runs reliably against the intermittently-
stalling dev HEM (KNOWN-ISSUES.md "Device stalls/hangs under sustained
load"), without hanging it. Two levers: (1) a client-side request pace —
`ehem_options.request_pace_ms` (REQ-NET-006) applied at the `do_send`
chokepoint, set by the test harness from `EHEM_TEST_PACE_MS`, defaulted to
150 ms by `./dev test it`; (2) whole-test stall-retry — `./dev` runs the
integration suite with `ctest --repeat until-pass:${EHEM_TEST_REPEAT:-3}`,
so a test that hits a transient stall re-runs after the device recovers
(the live tests self-clean, so re-runs are safe).

**Notes:** Diagnosed 2026-07-17 (session): the dev device intermittently
stalls under back-to-back load and, with its watchdog disabled, a
non-recovering stall needs a physical power-cycle. Per-operation probes
ruled out PQC / any single crypto op / connection volume as the cause. The
pace is a timing knob (like the timeouts), not a retry (retries stay the
caller's job, §7); the SDK reads no env — the harness passes the option.
Portable sleep (`nanosleep`/`Sleep`). Live-validated: full suite 11/11 green
with pacing, device alive throughout.

**Definition of done**
- [x] `ehem_options.request_pace_ms` implemented (append-only option → ctx →
      `do_send` pace), default 0; unit test for the wiring/ABI
      (test_options_request_pace); `./dev ci` gcc+clang + asan green;
      export/header gates green.
- [x] `src/proto_common.c` cross-compiles clean for `x86_64-w64-mingw32`
      (the new Windows `Sleep()` path).
- [x] `./dev test it` applies pacing (`EHEM_TEST_PACE_MS` default 150) and
      `ctest --repeat until-pass` stall-retry; `dev` passes `bash -n`.
- [x] Live-validated: full integration suite green with pacing on, device
      not hung (recorded in evidence).
- [x] KNOWN-ISSUES.md documents the device stall/hang finding + mitigation +
      upstream fixes to file.
