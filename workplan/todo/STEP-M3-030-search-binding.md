---
id: STEP-M3-030
title: "search binding — DESCR prefix/suffix/substring"
milestone: M3
implements: ["REQ-KEY-002"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M3-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_key_search(ctx, pattern, pattern_len, mode, offset,
limit, &out)` + full-walk variant over POST /api/keymgmt/search (scope
`keymgmt:search`), mode ∈ {PREFIX, SUFFIX, SUBSTRING} → raw-descr forms
`^<b64>` / `<b64>$` / `<b64>`. SDK base64-encodes the caller's raw bytes
and adds the anchor; result reuses the M3-010 page/entry structs.

**Notes:** Device 404 = "no keys matched" → EHEM_OK with an empty page
(python-observed; doc silent — precedence device > doc; the live test
confirms and the observed status code is recorded in REQ-KEY-002).
400/406/410 → mapped errors with payload. The unauthenticated bypass
(≥6-byte prefix + allow_keysearch) is deliberately NOT used in M3 —
every search authenticates; revisit at M8 for pre-login `^EXTAID`
discovery. Live test creates an EHEMTEST key with a known descr via the
M3-020 support helper, searches for its prefix, expects a hit; then
searches for a nonsense pattern, expects an empty result.

**Definition of done**
- [ ] Unit: request body `{descr, offset, limit}` byte-exact for all
      three modes (anchor placement + base64); page parses like list;
      404 → EHEM_OK + empty page; 400/406/410 mapped with payload; scope
      `keymgmt:search` declared; Authorization always sent.
- [ ] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
- [ ] Live: prefix search finds the created EHEMTEST key's descr; a
      no-match search returns empty — observed no-match status code
      recorded in REQ-KEY-002 (integration test, gated).
