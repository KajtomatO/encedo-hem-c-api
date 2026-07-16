---
id: STEP-M3-010
title: "keymgmt module + list binding (single page + full-repo walk)"
milestone: M3
implements: ["REQ-KEY-001"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M2-040"]
evidence:
  commits: []
  tests:
    - "tests/unit/test_keymgmt.c — full+minimal entry parse, descr base64 decode, missing kid/type → PROTOCOL, unknown field ignored, path construction, 401 re-acquire+retry, 403/406/409 mapping, multi-page walk (listed<limit mid-walk continues), single-page walk, arg guards, _free NULL-safe (12 cases)"
    - "tests/integration/test_keymgmt_live.c — live ehem_key_list_all against my.ence.do: 6 keys incl. protected TLS pair, kid(32-hex)/type populated (REQ-KEY-001 live gate)"
  notes: >
    New proto_keymgmt.c + public include/ehem/keymgmt.h. ehem_key_list builds
    GET /api/keymgmt/list[/{offset}[/{limit}]] (segments appended only when
    constrained; limit 0 → device default) under scope keymgmt:list via the
    shared request path (bearer, REQ-NET-005 recovery, HTTP→rc all inherited).
    Response {offset,total,listed,list[]} parsed by a shared parse_key_page()
    into a caller-owned ehem_key_page reused verbatim by search (REQ-KEY-002).
    Entry: kid/type required (missing → EHEM_ERR_PROTOCOL naming the field),
    label optional (NULL absent), descr std-base64-decoded to bytes+len (absent
    /empty → NULL/0; undecodable tolerated as absent), created/updated int64
    default 0. ehem_key_list_all walks in sub-cap pages of 10 (device 15-cap),
    merging entries by ownership transfer (realloc + detach), terminating on
    offset ≥ total or a zero-entry page — never listed<limit (python OQ-17).
    Unit 12/12 gcc+clang + ASan/LSan clean; export/header gates green; live
    walk green (total=6 listed=6). Live output recorded: TLS PrivateKey +
    TLS Certificate pair present as expected.
reopened: []
cancelled: null
---

**Goal:** New `proto_keymgmt.c` + `include/ehem/keymgmt.h`:
`ehem_key_list(ctx, offset, limit, &out)` returns one page as a
caller-owned `ehem_key_page` `{offset, total, listed, entries[]}` — each
entry `{kid (32-char hex string), type (device string), label, descr
(decoded bytes, optional), created, updated}` — and
`ehem_key_list_all(ctx, &out)` walks the whole repository into one merged
array. Both declare scope `keymgmt:list`; `_free` functions NULL-safe.

**Notes:** The device hard-caps `limit` at 15 server-side, so the full
walk terminates on `offset ≥ total` (or `listed == 0`) — never
`listed < limit` (python OQ-17: unreliable on observed firmware). `descr`
is absent-if-empty → NULL/0-length, decoded from base64. Tolerant parsing
like the other bindings (unknown fields ignored; missing `kid`/`type` in
an entry → `EHEM_ERR_PROTOCOL`). Fixtures from doc keymgmt/list.md.
Design the entry/page structs for reuse by search (REQ-KEY-002,
STEP-M3-030) — same response shape.

**Definition of done**
- [x] Fake-transport unit tests: full + minimal entry parse (absent descr
      → NULL/0), missing required entry field → PROTOCOL, unknown fields
      ignored, scope declaration, 401/403 per REQ-AUTH-003, 406/409 →
      EHEM_ERR_DEVICE with payload. (test_keymgmt.c — 12 cases.)
- [x] Full-walk unit test: multi-page fixtures paginate to completion;
      a mid-walk page with `listed < requested limit` does NOT terminate
      the walk. (test_list_all_multipage_walk — 25 keys over 3 pages,
      page 2 listed 8 < limit 10, walk continues to offset ≥ total.)
- [x] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
      (ehem_key_page_free(NULL) tested; ./dev test asan + ./dev check green.)
- [x] Live: `ehem_key_list_all` against the dev device returns its key
      population (≥ the protected TLS pair) with kid/type/label populated
      (integration test, gated per REQ-TEST-002). (test_keymgmt_live.c —
      6 keys incl. TLS PrivateKey/TLS Certificate, all kid/type populated.)
