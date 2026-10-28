---
id: REQ-TOOL-002
title: hem-tool reports certificate refresh and offers a checkin subcommand
status: superseded
priority: must
revision: 2   # 2026-10-28 (M13 decomposition, §6.2 approved with the hem-tool split plan 2026-10-27): superseded — hem-tool moved to encedo-hem-tool at 1.2.0; the same id there is authoritative; text frozen at rev 1.
source: user decision 2026-07-15 ("hem-tool should inform user if certificate was invalid and was refreshed")
depends_on: ["REQ-TOOL-001", "REQ-NET-005", "REQ-SYS-003"]
supersedes: null
superseded_by: "KajtomatO/encedo-hem-tool requirements/REQ-TOOL-002"
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool reports certificate refresh and offers a checkin subcommand

> **Superseded 2026-10-28 (M13 decomposition).** hem-tool moved to
> [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) at the SDK's 1.2.0 (user decision
> 2026-10-27); this requirement continues there under the same id and is
> maintained there. The text below is frozen at the revision it had when it
> left (rev 1); the code and tests that implemented it leave this repository
> in STEP-M13-010.

`hem-tool` SHALL inform the user whenever an operation involved an automatic
certificate recovery (REQ-NET-005), and SHALL provide an explicit `checkin`
subcommand that runs the check-in handshake on demand.

**Rationale:** A silent security-relevant event (the device presented an
invalid certificate; the SDK refreshed it) must be visible to the operator.
The explicit subcommand doubles as the manual driver for cert renewal, RTC
setting, and update discovery without needing the browser-based Manager.

**Acceptance criteria:**
- [ ] After any successful operation where `ehem_cert_refreshed(ctx)` is
      true, hem-tool prints a notice to stderr stating the certificate was
      invalid (expired) and was refreshed via check-in.
- [ ] `hem-tool checkin` runs `ehem_system_checkin()` and reports the result
      (certificate refreshed?, firmware/UI update available?, status);
      failures print `ehem_last_error` detail and exit nonzero.
- [ ] The notice and subcommand use only the public API (greppable).
