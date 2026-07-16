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
  tests: []
  notes: null
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
- [ ] Fake-transport unit tests: full + minimal entry parse (absent descr
      → NULL/0), missing required entry field → PROTOCOL, unknown fields
      ignored, scope declaration, 401/403 per REQ-AUTH-003, 406/409 →
      EHEM_ERR_DEVICE with payload.
- [ ] Full-walk unit test: multi-page fixtures paginate to completion;
      a mid-walk page with `listed < requested limit` does NOT terminate
      the walk.
- [ ] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
- [ ] Live: `ehem_key_list_all` against the dev device returns its key
      population (≥ the protected TLS pair) with kid/type/label populated
      (integration test, gated per REQ-TEST-002).
