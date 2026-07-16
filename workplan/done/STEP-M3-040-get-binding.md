---
id: STEP-M3-040
title: "get binding — public material by KID + live scope probe (keymgmt:use quirk)"
milestone: M3
implements: ["REQ-KEY-003"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M3-020"]
evidence:
  commits: []
  tests:
    - "tests/unit/test_keymgmt.c — get: 4 response shapes (asymmetric pubkey / CERT der / DER_PKEY der / symmetric none) w/ correct absent-field semantics + unknown-field-ignored + descr decode, missing type → PROTOCOL, 406 → NOT_FOUND, 403 → SCOPE_DENIED, per-kid scope keymgmt:use:<kid> + cache reuse (2 kids → 2 acquisitions, repeat = cache hit; 7-request assert), malformed kid → ARG (no I/O), arg guards, _free NULL-safe (9 get cases; 43 keymgmt total)"
    - "tests/integration/test_keymgmt_get_live.c — live get of a created EHEMTEST ED25519 key → type + 32-byte pubkey + updated (descr NOT returned — fw quirk); internal-linking test (like test_auth_live) that also PROBES keymgmt:get / keymgmt:gen scopes"
  notes: >
    ehem_key_get(ctx, kid, &out) → GET /api/keymgmt/get/{kid}, scope
    keymgmt:use:<kid> built per call (snprintf). ehem_key_details {type,updated
    always; pubkey XOR der material; descr optional} via parse_key_details +
    a new shared decode_opt_b64 (refactored parse_entry's descr decode onto it).
    406 → NOT_FOUND through a new shared map_406_not_found helper (delete
    refactored onto it too — payload stack-copied before ehem_ctx_fail). kid
    pre-validated 32-hex (ARG, no I/O).
    LIVE FINDINGS (device > doc, recorded in REQ-KEY-003 + ARCHITECTURE §12
    risk 3 + api_quirks):
    (1) GET omits `descr` on fw v1.2.2 — wire body is {type,pubkey,updated}
    only, even for a key created WITH a descr (firmware REPO_GetKey_byKID(...,0,
    ...) → descr_len 0). descr is list/search-only. Binding still decodes descr
    if present (future-proof).
    (2) SCOPE PROBE: fw v1.2.2-DIAG accepts BOTH documented prefix scopes
    keymgmt:get AND keymgmt:gen for get (each 200), not only keymgmt:use:<kid> —
    python OQ-16 is stale (firmware api_get_keymgmt_getkey confirms 3-way check).
    SDK still ships keymgmt:use:<kid> per REQ-KEY-003 (max compatibility, 1 token
    per kid); broader-scope optimization is an M4 §12-risk-3 decision.
    Unit 43/43 gcc+clang + ASan/LSan clean; MinGW cross-compile clean;
    export/header gates green; full integration 8/8 (incl. live get + probe).
reopened: []
cancelled: null
---

**Goal:** `ehem_key_get(ctx, kid, &out)` over GET /api/keymgmt/get/{kid}
parsing all four documented response shapes into one caller-owned struct:
`type` + `updated` always; at most one material field — `pubkey`
(asymmetric, decoded raw bytes) or `der` (CERT/DER_PKEY, decoded DER) or
neither (symmetric); optional `descr`; `label` never returned by the
firmware (comes from list/search). Requests exact scope
`keymgmt:use:<kid-hex>` per call.

**Notes:** Scope quirk: doc allows `keymgmt:get`/`keymgmt:gen`/exact
`keymgmt:use:<kid>`, but fw v1.2.2-DIAG accepted only the last (python
OQ-16; 403 otherwise) — device wins. The scope-keyed cache (REQ-AUTH-002)
handles per-kid scopes with no changes: one cache entry per kid, fine at
tens of keys. LIVE PROBE while here: try the documented `keymgmt:get`
scope against current firmware and record the outcome in REQ-KEY-003's
open criterion AND ARCHITECTURE §12 risk 3 (first concrete per-KID-scope
fact for M4). kid pre-validated (32 hex chars → EHEM_ERR_ARG, no I/O);
406 → EHEM_ERR_NOT_FOUND. Live target: the EHEMTEST ED25519 key from the
M3-020 helper (32-byte pubkey; created fresh in this test).

**Definition of done**
- [x] Unit: four response-shape fixtures parse with correct absent-field
      semantics; unknown fields ignored; malformed kid → EHEM_ERR_ARG
      without transport call; 406 → NOT_FOUND; 403 → SCOPE_DENIED.
      (test_get_* — 9 cases.)
- [x] Unit: requests exact scope `keymgmt:use:<kid>`; two kids → two
      token acquisitions, repeat get → cache reuse (request-count
      assertions). (test_get_per_kid_scope_cache — 7-request assert +
      scope decode.)
- [x] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
      (ehem_key_details_free(NULL) tested; asan + check green; MinGW clean.)
- [x] Live: get of a created EHEMTEST ED25519 key returns a 32-byte
      pubkey ~~+ descr~~ (integration test, gated). (test_keymgmt_get_live —
      32-byte pubkey confirmed; descr is NOT returned by GET on fw v1.2.2
      [device > doc], only via list/search — recorded in REQ-KEY-003.)
- [x] Live scope probe result recorded in REQ-KEY-003 and §12 risk 3
      (does current fw accept `keymgmt:get`/`keymgmt:gen` here?). (BOTH
      accepted on fw v1.2.2 — probe in test_keymgmt_get_live; recorded.)
