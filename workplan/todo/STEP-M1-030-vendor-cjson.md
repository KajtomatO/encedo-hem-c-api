---
id: STEP-M1-030
title: Vendor cJSON and add internal JSON helpers
milestone: M1
implements: ["REQ-BUILD-003"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Upstream cJSON vendored at `src/vendor/cjson/` with version and
license recorded, compiled into the library with hidden symbols, plus a
thin internal helper layer (parse/get-string/get-int/get-bool with
present/absent handling) that protocol bindings will use.

**Notes:** Record the upstream version and MIT license in a VENDORED.md or
file header. The helper layer is where tolerant-parsing behavior (ignore
unknown fields, report missing required fields) gets a single
implementation.

**Definition of done**
- [ ] `src/vendor/cjson/` contains pinned upstream cJSON.c/cJSON.h + provenance note
- [ ] Build uses only the vendored copy (no system lookup)
- [ ] cJSON symbols not exported from the shared library; no cJSON type in public headers
- [ ] Unit test round-trips a JSON document through the helper layer (label `unit`)
