---
id: REQ-AUTH-003
title: Authenticated request path — bearer injection and auth error mapping
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §5, §6, §7 ("no retries in 1.x beyond token re-acquisition and check-in recovery"); encedo-hem-api-doc (401/403 semantics)
depends_on: ["REQ-AUTH-001", "REQ-AUTH-002", "REQ-API-003", "REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#7-transport"]
---

# Authenticated request path — bearer injection and auth error mapping

Protocol bindings SHALL declare the scope a request requires; the common
request path (proto_common) obtains a token for that scope from the cache
(REQ-AUTH-002) and sends it as `Authorization: Bearer <token>`. Bindings
never handle tokens directly.

**Sanctioned retry:** if an authenticated request returns 401 with a cached
token (token invalidated server-side, e.g. after a reboot), the SDK drops
that cache entry, re-acquires once, and retries the request once. A 401 on
the retry maps to `EHEM_ERR_AUTH_FAILED`. This is the "token
re-acquisition" retry ARCHITECTURE §7 allows; it composes with (and does
not multiply) the REQ-NET-005 check-in retry.

**Mapping (REQ-API-003):** 401 → `EHEM_ERR_AUTH_FAILED` (after the retry
above; `EHEM_ERR_AUTH_EXPIRED` is produced client-side per REQ-AUTH-002);
403 → `EHEM_ERR_SCOPE_DENIED` with `ehem_last_error` carrying the device
payload.

**Acceptance criteria:**
- [ ] An authenticated binding sends `Authorization: Bearer` with the token
      for its declared scope; unauthenticated bindings (status, version,
      checkin) send no Authorization header (fake-transport unit tests).
- [ ] 401-with-cached-token: exactly one re-login and one retry, then
      `EHEM_ERR_AUTH_FAILED` if still rejected (unit test asserting the
      request sequence; recursion/composition guard with REQ-NET-005).
- [ ] 403 maps to `EHEM_ERR_SCOPE_DENIED` with device payload in
      `ehem_last_error` (unit test).
- [ ] The retry never re-sends a non-idempotent request that the device
      already executed: 401 means the request was rejected before
      processing, so the single retry is safe for all bindings (assert doc
      basis in notes; no code criterion).
