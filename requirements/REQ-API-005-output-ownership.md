---
id: REQ-API-005
title: Library-allocated outputs freed by matching free functions
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §4 (Memory)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
---

# Library-allocated outputs freed by matching free functions

Every output structure the library allocates SHALL have a matching
`ehem_*_free()` function that releases it completely.

**Rationale:** A single, uniform ownership rule ("library allocates, caller
frees via the matching function") avoids allocator-mismatch bugs across
DLL boundaries on Windows and keeps every binding's contract identical.
Conventions that ride along: byte buffers are `(uint8_t *ptr, size_t len)`
pairs; strings are NUL-terminated UTF-8. Zeroization of secret material is
specified with the auth requirements (M2), not here.

**Acceptance criteria:**
- [ ] Each M1 output struct (system status, system version, error detail if
      allocated) has a documented `ehem_*_free()`.
- [ ] Free functions accept NULL as a no-op.
- [ ] Unit suite runs leak-clean under ASan/LSan for every
      allocate-then-free pair.
