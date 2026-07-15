---
id: STEP-M1-050
title: Transport vtable + fake transport + first offline unit tests
milestone: M1
implements: ["REQ-NET-001", "REQ-TEST-001"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M1-040"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Vtable defined in one internal header; context options accept an override
- [ ] Fake transport in `tests/support/` with canned-response and request-capture support
- [ ] Unit test drives a request through ctx → vtable → fake and asserts both directions
- [ ] No direct libcurl usage outside the (future) default transport file — greppable
- [ ] All `unit`-labeled tests pass with no network access
