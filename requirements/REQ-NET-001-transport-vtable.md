---
id: REQ-NET-001
title: All network I/O behind an injectable transport vtable
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §7; §1 (transport injection decision)
depends_on: ["REQ-API-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#1-decisions-fixed"]
---

# All network I/O behind an injectable transport vtable

All HTTP(S) traffic SHALL pass through a transport vtable — create/destroy
plus a single send operation mapping a request (method, path, headers,
body, timeouts) to a response (status, headers, body) — which context
options can override with a caller-supplied implementation.

**Rationale:** The vtable seam is what makes every protocol and auth test
runnable offline (fake transport) and keeps libcurl swappable. The
transport knows nothing about JSON or authentication; headers arrive
prepared by the caller.

**Acceptance criteria:**
- [ ] The vtable type is defined in one place; protocol bindings reach the
      network only through it (no direct libcurl calls outside the default
      transport implementation — greppable).
- [ ] Context options accept a caller-supplied transport; when set, no
      libcurl code runs (unit tests exercise a binding end-to-end through
      the fake transport).
- [ ] The request carries per-call timeouts so later flows (mobile confirm)
      can pass longer waits without vtable changes.
