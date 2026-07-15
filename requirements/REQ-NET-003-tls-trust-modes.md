---
id: REQ-NET-003
title: Three TLS trust modes selectable per context
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §7 (TLS trust); §12 risk 4 (device certificate model unknown)
depends_on: ["REQ-NET-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Three TLS trust modes selectable per context

Context options SHALL offer exactly three TLS trust modes: system trust
store (the default), caller-supplied CA bundle or pinned certificate, and
an explicit opt-in insecure mode that skips verification.

**Rationale:** HEM devices may present self-signed or device-specific
certificates; which model the real device uses is open (§12 risk 4) and is
confirmed at the M1 gate. The three modes cover every outcome of that
verification: public CA (system trust), device CA / pinned cert
(caller-supplied), and lab use (insecure, never a silent default).

**Acceptance criteria:**
- [ ] Default context verifies against the system trust store; a
      certificate that fails verification yields a TLS-related failure
      (`EHEM_ERR_UNREACHABLE` or `EHEM_ERR_NETWORK`) with detail via
      `ehem_last_error`, not a crash or silent acceptance.
- [ ] A caller-supplied CA file/pinned certificate option is honored.
- [ ] Insecure mode requires an explicit option flag; nothing else disables
      verification.
- [ ] OPEN (M1 gate): the dev-machine HEM's actual certificate model is
      recorded here (self-signed? per-device CA?), and the mode that
      `hem-tool status` needs against it is documented. Cross-check the
      Python client's TLS handling (encedo-hem-python-api).
