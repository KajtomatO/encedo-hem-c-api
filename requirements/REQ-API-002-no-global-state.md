---
id: REQ-API-002
title: No mutable global state outside the context
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §1, §4 (context-based, no global state); HEM-GEN-5 (consumer extract)
depends_on: ["REQ-API-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#1-decisions-fixed"]
---

# No mutable global state outside the context

The library SHALL NOT keep mutable state outside `ehem_ctx` instances,
except the documented, idempotent `ehem_global_init` / `ehem_global_cleanup`
pair that exists only to wrap libcurl's process-global initialization.

**Rationale:** Contexts must be independent (multiple HEMs per process) and
the state layout must allow adding a per-context lock later without redesign
(mirrors HEM-GEN-5). `curl_global_init` is the one unavoidable process-global
step; it is isolated and documented rather than hidden.

**Acceptance criteria:**
- [ ] No file-scope mutable variables in library sources outside the
      global-init wrapper (reviewable by grep for `static` non-const data).
- [ ] `ehem_global_init`/`ehem_global_cleanup` are idempotent (unit test:
      double init, double cleanup, init-after-cleanup).
- [ ] All per-connection state is reachable only from the `ehem_ctx` struct.
