---
id: REQ-NET-002
title: Default transport implemented with libcurl
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §1 (libcurl decision), §7
depends_on: ["REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#1-decisions-fixed"]
---

# Default transport implemented with libcurl

The default transport implementation SHALL use libcurl, holding one curl
easy handle per context so connections are reused across requests.

**Rationale:** libcurl is mature and portable across the three target
platforms; connection reuse matters because network latency dominates at
HEM scale. Isolation behind the vtable (REQ-NET-001) keeps curl swappable
and out of the public ABI.

**Acceptance criteria:**
- [ ] Public headers contain no libcurl types and compile without libcurl
      development headers installed.
- [ ] One curl easy handle per context, created lazily or at context
      create, freed at context destroy (no handle leaks under ASan).
- [ ] Two sequential requests on one context reuse the connection
      (verifiable via curl verbose logging in a manual/integration check).
- [ ] HTTP status and response body are returned for both 2xx and non-2xx
      responses (error mapping happens above the transport).
