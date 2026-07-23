---
id: STEP-M8-030
title: ExtAuth login bindings — ehem_ext_request/token + live simulated login round-trip
milestone: M8
implements: ["REQ-AUTH-007"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M8-020"]
evidence:
  commits: ["2bee235"]
  tests: ["verifies: REQ-AUTH-007 — tests/unit/test_ext.c (+5 cases: unauthenticated wire shape, arg bounds, RTC-403 exactly-one recovery + no_auto_checkin opt-out, 401/406 AUTH_FAILED detail, token verbatim); tests/integration/test_ext_login_live.c (full live round-trip); tests/unit/test_ext_sim.c test_live_authreq_fixture (real device bytes offline)"]
  notes: "LIVE (my.ence.do fw v1.2.2): complete simulated login — request on a NEVER-logged-in ctx (no-auth bypass CONFIRMED), authreq sig verified via ECDH(eph,eid), claims asserted (3600 s plain / 900 s keymgmt:use rewrite with #-meta carrying our label), scheme-A entry decrypted, tampered authreply → 401, token minted (sub=base64(kid), exp=authreply exp, ctx echo) and USED on an authenticated config GET via direct transport send. Zero-pairing probe: 200 with scope:{} (curl). Fixture: real authreq + opening keys frozen (ext_authreq_fixture.h), unit-verified offline. §12 risk 6 core closed pending the M8-090 text edit. Unit 34/34 gcc+clang+ASan; export/header gates green; MinGW proto_ext.c syntax clean. FW note: authreq header key order {ecdh,typ,alg} (libjwt)."
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
- [x] Unit: shapes, arg pre-validation, 403-recovery composition (one
      checkin max, fake transport), error mapping
- [x] Live simulated login round-trip green unattended (integration
      label), bearer works (authenticated config GET via direct
      transport send), cleanup leaves no EXTAID/EHEMTEST residue
- [x] Scope-rewrite + no-auth + empty-object probes recorded in
      REQ-AUTH-007; live authreq fixture captured
      (ext_authreq_fixture.h) + unit-verified offline
- [x] `./dev ci` + asan green; export/header gates green; tags placed;
      evidence filled
