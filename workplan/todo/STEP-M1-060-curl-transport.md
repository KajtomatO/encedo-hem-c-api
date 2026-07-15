---
id: STEP-M1-060
title: libcurl default transport — TLS trust modes, timeouts
milestone: M1
implements: ["REQ-NET-002", "REQ-NET-003", "REQ-NET-004"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport"]
depends_on: ["STEP-M1-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `transport_curl.c` implementing the vtable with one curl easy
handle per context (connection reuse), the three TLS trust modes (system
default / caller CA or pinned cert / explicit insecure), and separate
connect + total-request timeouts, with curl failures translated to
transport-level error codes that map onto `EHEM_ERR_UNREACHABLE` /
`EHEM_ERR_NETWORK`.

**Notes:** Public headers must stay curl-free (compile check without curl
dev headers). Unit tests cover the option plumbing and error translation
via the seam; actual TLS behavior is exercised manually/integration
(STEP-M1-080, -100). Note curl_global_init is already wrapped by
`ehem_global_init` (STEP-M1-040).

**Definition of done**
- [ ] Vtable implementation with per-context easy handle; no handle leak (ASan)
- [ ] TLS modes settable via context options; insecure requires the explicit flag
- [ ] Connect and total timeouts applied per request from the request struct
- [ ] curl error codes translated: connect failure/timeout → unreachable-class, mid-transfer failure/timeout → network-class
- [ ] Public headers compile in a TU with no libcurl includes available
- [ ] Manual smoke against any HTTPS endpoint recorded in evidence.notes
