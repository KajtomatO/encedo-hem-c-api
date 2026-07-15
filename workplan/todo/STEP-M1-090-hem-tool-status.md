---
id: STEP-M1-090
title: hem-tool skeleton + status subcommand
milestone: M1
implements: ["REQ-TOOL-001"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M1-060", "STEP-M1-070"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** The `hem-tool` executable (in `src/tools/hem-tool/`) with a
`status` subcommand: connects via `--url`/`EHEM_URL`, applies TLS flags
(`--cacert <file>`, `--insecure`), prints live status and version fields,
exits nonzero with `ehem_last_error` detail on any failure.

**Notes:** Consumes only `include/ehem/` public headers — hem-tool doubles
as living documentation of the API and the manual driver for the M1 gate.
Argument parsing stays dependency-free (plain argv loop); subcommand
dispatch is structured so `keys list`/`keys rm` (M3) slot in without
rework.

**Definition of done**
- [ ] `hem-tool status --url https://...` prints status (uptime, temp, storage, hostname when present) and version (hardware, firmware, bootloader)
- [ ] `EHEM_URL` honored when `--url` absent; neither present → usage message, nonzero exit
- [ ] `--cacert` and `--insecure` map onto the context TLS options
- [ ] Unreachable/TLS/protocol failures print last-error detail and exit nonzero
- [ ] Only public headers included (greppable)
