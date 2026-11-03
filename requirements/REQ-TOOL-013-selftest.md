---
id: REQ-TOOL-013
title: hem-tool selftest — run the device self-test and report health
status: superseded
priority: should
revision: 2   # 2026-10-28 (M13 decomposition, §6.2 approved with the hem-tool split plan 2026-10-27): superseded — hem-tool moved to encedo-hem-tool at 1.2.0; the same id there is authoritative; text frozen at rev 1.
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-007; approved 2026-07-17
depends_on: ["REQ-SYS-007"]
supersedes: null
superseded_by: "KajtomatO/encedo-hem-tool requirements/REQ-TOOL-013"
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool selftest — run the device self-test and report health

> **Superseded 2026-10-28 (M13 decomposition).** hem-tool moved to
> [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) at the SDK's 1.2.0 (user decision
> 2026-10-27); this requirement continues there under the same id and is
> maintained there. The text below is frozen at the revision it had when it
> left (rev 1); the code and tests that implemented it leave this repository
> in STEP-M13-010.

`hem-tool selftest` SHALL run `ehem_system_selftest` and report the
device's health verdict, exiting non-zero when the device itself reports
a fail state.

- Prints: `fls_state` (with a PASS/FAIL word), the four timestamps
  (human-readable UTC), `kat_busy` when set, `se_state` when present,
  and the `repo_stats` block (total/deleted/invalid/fragmented/
  freeslots) — the only place key-slot exhaustion is visible.
- **Exit code carries the verdict:** 0 = reachable AND `fls_state == 0`;
  1 = SDK/device error; 3 = selftest ran but `fls_state != 0` (distinct
  code so scripts can tell "unhealthy" from "unreachable", mirroring the
  cert-install multi-code convention).
- Warns on stderr that the call re-runs the battery on-device (side
  effect), so operators don't hammer it in tight loops.
- Credentials via the standard flags/env.

**Rationale:** living documentation for REQ-SYS-007 and a scriptable
health gate — the natural companion to `hem-tool status` for the
device's own self-verdict (status reports reachability, selftest reports
integrity).

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport, 2026-07-18): healthy fixture
      → exit 0 with PASS + repo stats; `fls_state = 2` → exit 3 with
      FAIL; device 500 → exit 1; missing passphrase → exit 2
      (tests/unit/test_tool_m7.c).
- [x] Live demo (2026-07-18): `hem-tool selftest` exited 0 — PASS,
      UTC-rendered battery timestamps, secure-enclave state 0, repo
      stats (5 keys / 1584 free slots) consistent with the key count.
- [x] `--help` updated (the usage text is the CLI reference; the README
      carries no per-subcommand table by design).
