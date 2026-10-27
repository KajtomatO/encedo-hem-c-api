---
id: REQ-API-002
title: No mutable global state outside the context
status: verified
priority: must
revision: 3
source: ARCHITECTURE.md §1, §4 (context-based, no global state); HEM-GEN-5 (consumer extract); rev 3 — wording 2026-10-12 (M12 decomposition, §6.2 approved in chat; the obligation is unchanged, status kept): the global-init pair is described backend-generically — wolfCrypt's `wolfCrypt_Init` on the default backend, `OPENSSL_init_crypto` on the OpenSSL backend of REQ-BUILD-007 (whose cleanup is a deliberate no-op so the pair stays idempotent) — and REQ-API-010's compatibility check runs inside `ehem_global_init` without touching the global state when it refuses
depends_on: ["REQ-API-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#1-decisions-fixed"]
---

# No mutable global state outside the context

The library SHALL NOT keep mutable state outside `ehem_ctx` instances,
except the documented, idempotent `ehem_global_init` / `ehem_global_cleanup`
pair that exists only to wrap the backends' process-global initialization
(libcurl's `curl_global_init`; the crypto backend's — wolfCrypt's
`wolfCrypt_Init` on the default backend, `OPENSSL_init_crypto` on the
OpenSSL backend of REQ-BUILD-007).

**Rationale:** Contexts must be independent (multiple HEMs per process) and
the state layout must allow adding a per-context lock later without redesign
(mirrors HEM-GEN-5). `curl_global_init` and the crypto backend's init are
the two unavoidable process-global steps (`wolfCrypt_Init` initializes
wolfSSL's global mutexes, mandatory on Windows); they are isolated and
documented rather than hidden. *(Rev 3, 2026-10-12: on OpenSSL the
cleanup is a no-op — `OPENSSL_cleanup` is one-shot and would break
init-after-cleanup; REQ-API-010's compatibility check runs inside
`ehem_global_init` and a refused init leaves the global state untouched,
so cleanup after a refusal is a no-op and a later init re-checks — the
idempotency criterion below is extended by that REQ.)*

**Acceptance criteria:**
- [ ] No file-scope mutable variables in library sources outside the
      global-init wrapper (reviewable by grep for `static` non-const data).
- [ ] `ehem_global_init`/`ehem_global_cleanup` are idempotent (unit test:
      double init, double cleanup, init-after-cleanup).
- [ ] All per-connection state is reachable only from the `ehem_ctx` struct.
