---
id: REQ-TOOL-001
title: hem-tool status subcommand
status: superseded
priority: must
revision: 2   # 2026-10-28 (M13 decomposition, §6.2 approved with the hem-tool split plan 2026-10-27): superseded — hem-tool moved to encedo-hem-tool at 1.2.0; the same id there is authoritative; text frozen at rev 1.
source: ARCHITECTURE.md §8; §11 M1 gate
depends_on: ["REQ-SYS-001", "REQ-SYS-002"]
supersedes: null
superseded_by: "KajtomatO/encedo-hem-tool requirements/REQ-TOOL-001"
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool status subcommand

> **Superseded 2026-10-28 (M13 decomposition).** hem-tool moved to
> [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) at the SDK's 1.2.0 (user decision
> 2026-10-27); this requirement continues there under the same id and is
> maintained there. The text below is frozen at the revision it had when it
> left (rev 1); the code and tests that implemented it leave this repository
> in STEP-M13-010.

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
