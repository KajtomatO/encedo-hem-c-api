---
id: STEP-M8-030
title: ExtAuth login bindings — ehem_ext_request/token + live simulated login round-trip
milestone: M8
implements: ["REQ-AUTH-007"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M8-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_ext_request` and `ehem_ext_token` unauthenticated
bindings with the 403-RTC single checkin+retry (shared REQ-AUTH-004
budget), live-proven by the complete simulated login: pair → request →
decrypt own scheme-A entry → authreply → token → bearer exercised on
`GET /api/system/config`.

**Notes:** this closes the core of §12 risk 6. Live extras: (a) probe
both endpoints with NO Authorization header (routing source missing —
confirm the bypass); (b) `keymgmt:use:<kid>` scope-rewrite probe —
`#`-meta suffix visible after decrypt, 15-min exp in authreq AND issued
bearer, vs 60-min plain scope; (c) capture the empty-`scope`-object
shape with zero pairings; (d) capture a REAL authreq as the fixture
promised at M8-010; (e) record bearer `sub`=base64(kid). Map 401/406 →
`EHEM_ERR_AUTH_FAILED` with detail. The tester's ignored-`exp`
divergence and the dead anti-bruteforce delay are already recorded in
REQ-AUTH-007 — verify nothing contradicts them live.

**Definition of done**
- [ ] Unit: shapes, arg pre-validation, 403-recovery composition (one
      checkin max, fake transport), error mapping
- [ ] Live simulated login round-trip green unattended (integration
      label), bearer works, cleanup leaves no EXTAID/EHEMTEST residue
- [ ] Scope-rewrite + no-auth + empty-object probes recorded in
      REQ-AUTH-007; live authreq fixture captured
- [ ] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
