---
id: STEP-M1-050
title: Transport vtable + fake transport + first offline unit tests
milestone: M1
implements: ["REQ-NET-001", "REQ-TEST-001"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M1-040"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-NET-001, REQ-TEST-001 (tests/unit/test_transport.c)"]
  notes: >
    Vtable in one internal header src/transport.h: ehem_http_method,
    ehem_header, ehem_request (method/path/headers/body + per-request
    connect+total timeouts — REQ-NET-001 so M8's long wait needs no vtable
    change), ehem_response (status/headers/body, body NUL-terminated),
    ehem_transport_ops {send,destroy} + the ehem_transport instance
    {ops,state}. Dispatch + ownership in src/transport.c: ehem_transport_send
    (NULL/arg guards → EHEM_ERR_ARG), ehem_transport_destroy (runs
    ops->destroy then frees the wrapper), ehem_response_free (NULL-safe, zeroes
    the struct so double-free is safe). Contract: connection-level failure →
    EHEM_ERR_UNREACHABLE/_NETWORK with a zeroed response; any HTTP status
    (incl. 4xx/5xx) → EHEM_OK with body returned for the binding to map.
    Context wiring: ehem_options.transport override is stored as the effective
    transport (borrowed; owns_transport=false), reachable via
    ehem_ctx_transport(); the built-in default (owned) is wired at STEP-M1-060.
    Fake transport in tests/support/fake_transport.{h,c} (built into the
    reusable ehem_test_support lib): FIFO response script (transport error OR
    status+body) + deep request capture (method/path/headers/body) with
    header lookup. Verified on Linux (2026-07-15): `ctest -L unit` 5/5 green
    (test_transport drives ctx→vtable→fake asserting both directions: GET
    roundtrip, POST body capture, transport-error passthrough, arg guards),
    clean under GCC ASan/UBSan, `nm -D` still exports only ehem_* symbols, and
    `grep -rn curl src/` finds only planning comments (no libcurl code outside
    the future transport_curl.c).
reopened: []
cancelled: null
---

**Goal:** The transport vtable type (create/destroy + send:
request{method, path, headers, body, timeouts} → response{status, headers,
body}), injectable via context options, plus a fake transport in
`tests/support/` (canned responses, outgoing-request capture) proving the
seam works end to end offline.

**Notes:** The request struct carries per-call timeouts from day one so
the mobile-confirm flow (M8) needs no vtable change. Response bodies are
returned for non-2xx statuses too — error mapping happens above the
transport. The fake supports scripting a sequence of responses and
asserting on captured requests (method, path, headers, body).

**Definition of done**
- [x] Vtable defined in one internal header; context options accept an override — *src/transport.h; ehem_options.transport → ehem_ctx_transport()*
- [x] Fake transport in `tests/support/` with canned-response and request-capture support — *fake_transport.{h,c}: FIFO script + deep request capture*
- [x] Unit test drives a request through ctx → vtable → fake and asserts both directions — *tests/unit/test_transport.c*
- [x] No direct libcurl usage outside the (future) default transport file — greppable — *`grep -rn curl src/` → only comments*
- [x] All `unit`-labeled tests pass with no network access — *`ctest -L unit` 5/5, ASan/UBSan clean*
