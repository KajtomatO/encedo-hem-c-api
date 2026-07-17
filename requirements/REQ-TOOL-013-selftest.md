---
id: REQ-TOOL-013
title: hem-tool selftest — run the device self-test and report health
status: approved
priority: should
revision: 1
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-007; approved 2026-07-17
depends_on: ["REQ-SYS-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool selftest — run the device self-test and report health

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
- [ ] Unit (hem-tool-core, fake transport): healthy fixture → exit 0
      with PASS output; `fls_state != 0` fixture → exit 3; transport
      error → exit 1; repo_stats rendered; kat_busy/se_state
      presence-dependent lines.
- [ ] Live demo: `hem-tool selftest` against my.ence.do exits 0 and
      prints repo stats consistent with `keys list`.
- [ ] README tool table + `--help` updated.
