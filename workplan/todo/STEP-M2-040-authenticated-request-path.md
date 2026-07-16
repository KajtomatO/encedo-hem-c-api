---
id: STEP-M2-040
title: "Authenticated request path: per-binding scope, bearer injection, single 401 re-acquire retry"
milestone: M2
implements: ["REQ-AUTH-003"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M2-030"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_proto_request_raw/json` (proto_common) accept an optional
scope: when non-NULL, obtain a token via `ehem_auth_ensure_token` and send
`Authorization: Bearer`. On 401 with a cached token: drop the cache entry,
re-login once, retry once; second 401 → `EHEM_ERR_AUTH_FAILED`. 403 →
`EHEM_ERR_SCOPE_DENIED`. Existing unauthenticated bindings (status,
version, checkin) pass NULL scope and are unchanged.

**Notes:** Composition guard with REQ-NET-005: the auth retry and the
check-in retry must not multiply (max one of each per request; assert via
sequence-counting unit test). Also add the first live integration test of
M2: `test_auth_live` — login + a scoped call (config GET arrives in
M2-050, so use token acquisition itself: assert `ensure_token` returns a
parseable JWT with the requested scope claim), gated on EHEM_TEST_URL +
EHEM_TEST_PASSPHRASE per REQ-TEST-002.

**Definition of done**
- [ ] Fake-transport unit tests: bearer present for scoped requests,
      absent for NULL-scope; 401→re-login→retry→AUTH_FAILED sequence;
      403→SCOPE_DENIED with payload; auth×checkin retry composition guard.
- [ ] `test_auth_live` green against the dev device (records token `sub`
      claim into REQ-AUTH-001 open criterion at the gate).
- [ ] ASan/LSan + export checks green on GCC + Clang.
