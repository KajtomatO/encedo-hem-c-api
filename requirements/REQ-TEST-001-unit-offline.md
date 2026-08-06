---
id: REQ-TEST-001
title: Unit suite runs offline through injected fake transport
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §9 (Testing policy); §1 (CMocka decision)
depends_on: ["REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
---

# Unit suite runs offline through injected fake transport

Unit tests (CTest label `unit`) SHALL run entirely without network access,
exercising the library through an injected fake transport.

**Rationale:** Offline unit tests run on every build on both platforms and
in CI without a device. The fake transport (canned request→response pairs,
request capture for assertions) lives in `tests/support/` and is the
concrete payoff of the vtable seam (REQ-NET-001).

**Acceptance criteria:**
- [ ] A fake transport exists in `tests/support/` supporting canned
      responses and capture of the outgoing request for assertions.
- [ ] All tests labeled `unit` pass with no network interface use (no real
      sockets; verifiable by running with networking disabled).
- [ ] Unit tests are written in CMocka and run via `ctest -L unit` on
      Linux and Windows (MinGW).
