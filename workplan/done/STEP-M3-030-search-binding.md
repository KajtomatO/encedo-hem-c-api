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
  tests:
    - "tests/unit/test_keymgmt.c — search: prefix/suffix/substring body byte-exact (^b64 / b64$ / b64) + {descr,offset,limit}, scope keymgmt:search (eJWT decode) + Authorization, 404 → EHEM_OK+empty page, 400/410 → DEVICE w/ payload, full-walk (listed<limit mid-walk continues; offset advances 0/10/18), no-match walk → empty, arg guards (NULL ctx/out, bad mode, NULL pattern w/ len>0) (9 search cases; 34 keymgmt total)"
    - "tests/integration/test_keymgmt_search_live.c — live: create EHEMTEST key (descr=label) → prefix search finds it → no-match search returns empty (REQ-KEY-002 live gate)"
  notes: >
    ehem_key_search(ctx, pattern, pattern_len, mode, offset, limit, &out) +
    ehem_key_search_all: POST /api/keymgmt/search scope keymgmt:search. mode enum
    {SUBSTRING=0, PREFIX, SUFFIX} → SDK std-base64-encodes the raw pattern and
    adds the '^'/'$' anchor (build_search_descr); body {descr,offset,limit}
    (offset/limit sent verbatim, device caps 15). Result reuses the M3-010
    ehem_key_page/parse_key_page + page_append walk; search_one_page maps a 404
    to EHEM_OK+empty page (defensive) and clears the error. Single-page +
    full-walk variants, empty-pattern allowed (device decides).
    FIRMWARE FINDING (REQ-KEY-002 no-match code): encedo_firmware api_keymgmt.c
    returns 200 with an empty list for zero matches (ret>=0) and 410 only for a
    filter FAILURE (ret<0) — there is no 404 from this handler on fw v1.2.2. The
    python client's "404 = no-match" is older firmware; our 404→empty mapping is
    kept defensively but the live path is 200-empty (handled by parse_key_page).
    Recorded in REQ-KEY-002 + api_quirks memory.
    Unit 34/34 gcc+clang + ASan/LSan clean; MinGW cross-compile clean;
    export/header gates green; full integration 7/7 (incl. live search).
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
- [x] Unit: request body `{descr, offset, limit}` byte-exact for all
      three modes (anchor placement + base64); page parses like list;
      404 → EHEM_OK + empty page; 400/406/410 mapped with payload; scope
      `keymgmt:search` declared; Authorization always sent. (test_search_*
      — 9 cases; 410 covered, 406 subsumed by the shared >=400 → DEVICE map.)
- [x] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
      (page reuse → ehem_key_page_free; ./dev test asan + ./dev check green;
      MinGW cross-compile of proto_keymgmt.c clean.)
- [x] Live: prefix search finds the created EHEMTEST key's descr; a
      no-match search returns empty — observed no-match status code
      recorded in REQ-KEY-002 (integration test, gated). (test_keymgmt_search_live
      — prefix matched exactly the created key; no-match empty. Observed:
      200-empty on fw v1.2.2, per firmware source + REQ-KEY-002.)
