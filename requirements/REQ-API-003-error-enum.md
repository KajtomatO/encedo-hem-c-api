---
id: REQ-API-003
title: Single error enum distinguishing consumer-required conditions
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §4; HEM-SDK-8 and HEM-ERR-1 (requirements/start_point/encedo-pkcs11/REQUIREMENTS-hem.md)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
---

# Single error enum distinguishing consumer-required conditions

Every fallible public API function SHALL return a status from one error
enum, `ehem_rc`, whose values distinguish at minimum: `EHEM_OK`,
`EHEM_ERR_NETWORK`, `EHEM_ERR_UNREACHABLE`, `EHEM_ERR_AUTH_EXPIRED`,
`EHEM_ERR_AUTH_FAILED`, `EHEM_ERR_SCOPE_DENIED`, `EHEM_ERR_USER_REJECTED`,
`EHEM_ERR_CONFIRM_TIMEOUT`, `EHEM_ERR_NOT_FOUND`, `EHEM_ERR_DEVICE`,
`EHEM_ERR_PROTOCOL`, `EHEM_ERR_ARG`, `EHEM_ERR_NOMEM`,
`EHEM_ERR_UNSUPPORTED`.

**Rationale:** The PKCS#11 backend must implement its HEM-ERR-1 mapping
table without guessing (HEM-SDK-8): user rejection vs. confirm timeout vs.
expired credential vs. scope denied vs. network failure vs. device
unreachable must be distinct values. The full enum ships in M1 even though
several values only become producible in later milestones — the ABI surface
is fixed early.

**Acceptance criteria:**
- [ ] `ehem_rc` with at least the values above is declared in the public
      headers; `EHEM_OK == 0`.
- [ ] Every public function returning a status uses `ehem_rc`.
- [ ] `ehem_rc_str()` (or equivalent) maps every value to a stable string
      (unit test iterates the enum).
- [ ] Transport-level failures are distinguished in M1: connection
      failure/refused → `EHEM_ERR_UNREACHABLE`; established-but-failed
      exchange → `EHEM_ERR_NETWORK`; malformed/unparseable response →
      `EHEM_ERR_PROTOCOL` (unit tests via fake transport).
