---
id: REQ-API-004
title: Retrievable last-error detail on the context
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §4 (Errors)
depends_on: ["REQ-API-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
---

# Retrievable last-error detail on the context

After a failed call, the context SHALL make retrievable — via
`ehem_last_error` — the last HTTP status code, the device error payload (if
any), and a human-readable message.

**Rationale:** The enum (REQ-API-003) is for programmatic branching; humans
debugging a failure need the raw HTTP status and device error body. Keeping
detail on the context (not in every return) keeps signatures simple, which
is acceptable under the 1.x single-thread-per-context rule.

**Acceptance criteria:**
- [ ] `ehem_last_error` returns HTTP status, device error payload, and
      message after a failing call (unit test with fake transport returning
      an HTTP error).
- [ ] Detail is valid until the next API call on the same context and is
      owned by the context (caller does not free it).
- [ ] After a successful call the detail is reset/empty.
