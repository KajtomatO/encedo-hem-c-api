---
id: REQ-KEY-002
title: Binding for /api/keymgmt/search — DESCR pattern search
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6; encedo-hem-api-doc keymgmt/search.md; encedo-hem-python-api keymgmt.py (404 = no-match behavior); HEM-SDK-4 (DESCR-prefix search); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/keymgmt/search — DESCR pattern search

The SDK SHALL provide `ehem_key_search(ctx, pattern, pattern_len, mode,
offset, limit, &out)` (plus a full-walk variant mirroring REQ-KEY-001)
over `POST /api/keymgmt/search` (scope `keymgmt:search`), where
`mode ∈ {PREFIX, SUFFIX, SUBSTRING}` selects the documented raw-`descr`
forms — `^<b64>`, `<b64>$`, `<b64>` respectively. The SDK base64-encodes
the caller's raw pattern bytes and adds the anchor character itself;
callers never handle base64. The response is the same page shape and
entry struct as REQ-KEY-001.

**Device no-match behavior:** the python client observed HTTP **404 =
"no keys matched"** and treats it as an empty result; the doc does not
list 404 for this endpoint (it lists 410 "repo filter failure").
Precedence device > doc: the binding maps 404 to `EHEM_OK` with an empty
page. **Live-confirmed (2026-07-16, firmware v1.2.2-DIAG):** this firmware
returns **HTTP 200 with an empty `list`** for zero matches — NOT 404 (that
was the python client's older-firmware observation) — and reserves **410**
for an actual filter *failure* (confirmed in encedo_firmware
`api_keymgmt.c`: `ret>=0 → 200(list)`, `ret<0 → 410`). The 200-empty case is
handled by the ordinary page parse; the 404→empty mapping is retained
defensively for firmware that behaves as python observed. Both yield an
empty page.

**Rationale:** DESCR-prefix search is an explicit consumer requirement
(HEM-SDK-4; pkcs11 `hem_find_objects`). The doc's unauthenticated bypass
(prefix ≥ 6 decoded bytes + device `allow_keysearch`) is how the Manager
finds paired authenticators pre-login; the SDK does not use the bypass in
M3 — every search authenticates. Revisit at M8 (ext-auth flows need
pre-login `^EXTAID` discovery).

**Acceptance criteria:**
- [x] Request body carries `{descr, offset, limit}` with the correct
      anchor/base64 shape for each of the three modes (fake-transport unit
      tests asserting exact body bytes). — test_search_prefix_body
      (`^RVhUQUlE`), _suffix_body (`RVhUQUlE$`), _substring_body (`RVhUQUlE`).
- [x] Result page parses like REQ-KEY-001; 404 → `EHEM_OK` + empty page;
      400/406/410 → mapped errors with device payload (unit tests). —
      test_search_404_empty, test_search_400_device, test_search_410_device;
      page parse shared with list (parse_key_page).
- [x] Declares scope `keymgmt:search`; Authorization header always sent
      (unit test). — test_search_prefix_body (eJWT decode → `keymgmt:search`,
      Authorization asserted).
- [x] Live: prefix search for the `EHEMTEST`-labeled test key's descr
      returns it; a search matching nothing returns an empty result
      (integration test — also confirms the 404-as-no-match mapping;
      record the observed status code here). — test_keymgmt_search_live
      (prefix matched exactly the created key; no-match returned empty).
      OBSERVED no-match status: **200 with empty list** on firmware v1.2.2
      (see "Device no-match behavior" above), not 404.
