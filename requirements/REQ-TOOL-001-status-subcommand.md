---
id: REQ-TOOL-001
title: hem-tool status subcommand
status: implemented
priority: must
revision: 1
source: ARCHITECTURE.md §8; §11 M1 gate
depends_on: ["REQ-SYS-001", "REQ-SYS-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool status subcommand

`hem-tool` SHALL provide a `status` subcommand that connects to the HEM
given by `--url` (or the `EHEM_URL` environment variable) and prints the
device's live status and version information.

**Rationale:** The M1 gate: proof of a working end-to-end path (public API →
transport → real device → parsed structs). hem-tool consumes the public API
only — it doubles as living documentation and the manual integration
driver. TLS trust options must be reachable from the CLI so the M1 gate can
confirm the device certificate model (REQ-NET-003).

**Acceptance criteria:**
- [ ] `hem-tool status --url https://...` prints status fields (uptime,
      temperature, storage, hostname when present) and version fields
      (hardware, firmware, bootloader).
- [ ] `EHEM_URL` is honored when `--url` is absent; missing both yields a
      usage error and nonzero exit code.
- [ ] TLS options (CA file / insecure) are available as flags and map to
      the context options.
- [ ] Failure paths (unreachable, TLS failure, protocol error) print the
      `ehem_last_error` detail and exit nonzero.
- [ ] hem-tool sources include only `include/ehem/` public headers
      (greppable — no internal headers).
