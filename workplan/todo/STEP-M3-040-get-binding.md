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
  tests: []
  notes: null
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
- [ ] Unit: four response-shape fixtures parse with correct absent-field
      semantics; unknown fields ignored; malformed kid → EHEM_ERR_ARG
      without transport call; 406 → NOT_FOUND; 403 → SCOPE_DENIED.
- [ ] Unit: requests exact scope `keymgmt:use:<kid>`; two kids → two
      token acquisitions, repeat get → cache reuse (request-count
      assertions).
- [ ] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
- [ ] Live: get of a created EHEMTEST ED25519 key returns a 32-byte
      pubkey + descr (integration test, gated).
- [ ] Live scope probe result recorded in REQ-KEY-003 and §12 risk 3
      (does current fw accept `keymgmt:get`/`keymgmt:gen` here?).
