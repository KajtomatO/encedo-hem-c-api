---
id: REQ-NET-006
title: Optional client-side request pacing
status: verified
priority: should
revision: 1
source: user decision 2026-07-17 (mitigate dev-HEM stalls/hangs under back-to-back load — see KNOWN-ISSUES.md; approved 2026-07-17); ARCHITECTURE.md §7 (timeouts are options; retries are the caller's)
depends_on: ["REQ-API-001", "REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport"]
---

# Optional client-side request pacing

The SDK SHALL honor an `ehem_options.request_pace_ms` (long, milliseconds)
that imposes a minimum delay before dispatching **each** HTTP request,
including retried dispatches. `0` (the default, per the `abi_size`
append-only discipline) disables pacing — the SDK never delays on its own.
The value is copied into the context at creation like the connect/total
timeouts, and applied at the single request chokepoint (`do_send`).

Pacing is a **throttle**, not a retry policy: it only inserts delay, never
changes status mapping, retry counts, or semantics. It exists because some
devices — notably the development HEM (firmware v1.2.2) — intermittently
stall or hang when API requests arrive back-to-back (KNOWN-ISSUES.md
"Device stalls/hangs under sustained load"); a small pace reduces that.

The SDK itself reads no environment or config (ARCHITECTURE §1): the value
arrives as an option parameter. The integration-test harness sets it from
`EHEM_TEST_PACE_MS`, and `./dev test it` defaults that to 150 ms.

**Rationale:** user decision 2026-07-17, during the investigation of the
dev device hanging under the M5 integration suite. The controlled probes
that paced themselves survived hundreds of operations where the unpaced
back-to-back suite hung the device. Pacing is the client-side half of the
mitigation; the stall-retry half is `ctest --repeat until-pass` in `./dev`.
Framed as a general option (rate-sensitive devices), not test-only.

**Acceptance criteria:**
- [x] `ehem_options_init` defaults `request_pace_ms` to 0; a set value is
      copied into the context; an options block whose `abi_size` predates
      the field leaves the default (append-only ABI) — unit test.
      — test_context: test_options_request_pace (gcc+clang+asan green).
- [x] With a nonzero pace, each request is delayed by at least that long
      (the chokepoint sleeps before every dispatch, retries included);
      with pace 0 there is no added delay — verified functionally: the live
      integration suite ran with `EHEM_TEST_PACE_MS=150` (11/11 green, 167 s,
      device alive throughout — 2026-07-17); a timing assertion is avoided
      in unit tests for MinGW clock-resolution robustness.
- [x] Portable: the delay uses `nanosleep` on POSIX and `Sleep` on Windows
      (MinGW); `src/proto_common.c` cross-compiles clean for
      `x86_64-w64-mingw32` (verified 2026-07-17); Windows CI unit leg green
      on push.
