---
id: REQ-API-001
title: Opaque context handle with create/destroy lifecycle
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §4; HEM-SDK-1 (requirements/start_point/encedo-pkcs11/REQUIREMENTS-hem.md)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
---

# Opaque context handle with create/destroy lifecycle

The SDK SHALL expose an opaque context handle (`ehem_ctx`), created from a
device URL plus an options structure and destroyed by a single call that
releases every resource the context owns.

**Rationale:** One context = one HEM instance. encedo-pkcs11 needs one
connection per slot, i.e. several independent contexts in one process
(HEM-SDK-1, HEM-CFG-1 upstream). An opaque handle keeps the ABI stable and
hides all third-party types.

**Acceptance criteria:**
- [ ] `ehem_ctx` is opaque in public headers (forward-declared struct only).
- [ ] Create takes a URL and an options struct (timeouts, TLS trust,
      transport override); invalid arguments return `EHEM_ERR_ARG`.
- [ ] Two contexts for different URLs coexist in one process without
      interference (unit test via fake transport).
- [ ] Destroy frees all context resources; destroy of NULL is a safe no-op
      (verified with ASan/LSan in the unit suite).
