---
id: REQ-NET-004
title: Separate connect and total-request timeouts
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §7 (Timeouts)
depends_on: ["REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport"]
---

# Separate connect and total-request timeouts

Context options SHALL provide two separate timeouts — connection
establishment and total request duration — applied by the transport to
every request.

**Rationale:** "Device unreachable" (connect timeout) and "device slow /
network failure mid-operation" (total timeout) map to different consumer
error codes (HEM-ERR-1 upstream). The transport request struct carries
per-call timeouts (REQ-NET-001) so the mobile-confirm flow can later pass
its longer wait explicitly without redesign.

**Acceptance criteria:**
- [ ] Both timeouts are settable in context options with documented
      defaults.
- [ ] Connect timeout expiry against a non-routable address yields
      `EHEM_ERR_UNREACHABLE` (integration or manual check; unit test via
      fake transport simulating the transport-level timeout code).
- [ ] Total-request timeout expiry yields `EHEM_ERR_NETWORK` with timeout
      detail retrievable via `ehem_last_error`.
