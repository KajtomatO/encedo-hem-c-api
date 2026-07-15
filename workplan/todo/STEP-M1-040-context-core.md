---
id: STEP-M1-040
title: Context core — ehem_ctx lifecycle, options, error enum, last-error
milestone: M1
implements: ["REQ-API-001", "REQ-API-002", "REQ-API-003", "REQ-API-004"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_ctx` create/destroy with an options structure (URL,
timeouts, TLS trust mode, transport override slot), the complete `ehem_rc`
enum with `ehem_rc_str()`, `ehem_last_error` detail storage, and idempotent
`ehem_global_init`/`ehem_global_cleanup`.

**Notes:** The full 14-value enum ships now even though auth values only
become producible in M2+ — the ABI surface is fixed early (REQ-API-003
rationale). Options struct extensibility (how it may grow before 1.0) gets
a first answer here; keep it consistent with the §4 size/version
discipline note. All mutable state lives in the ctx struct (REQ-API-002).

**Definition of done**
- [ ] Create/destroy with URL validation (`EHEM_ERR_ARG` on bad input); destroy(NULL) safe
- [ ] Two contexts coexist independently (unit test)
- [ ] `ehem_rc` complete; `ehem_rc_str()` covers every value (iterating unit test)
- [ ] `ehem_last_error` returns HTTP status/device payload/message; reset on success (unit tests, fake transport may be stubbed locally until STEP-M1-050 lands)
- [ ] Global init/cleanup idempotent (double-init/double-cleanup unit test)
- [ ] Unit suite ASan/LSan-clean on Linux
