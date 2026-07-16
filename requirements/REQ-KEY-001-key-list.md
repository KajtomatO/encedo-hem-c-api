---
id: REQ-KEY-001
title: Binding for /api/keymgmt/list — paginated key inventory
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M3); encedo-hem-api-doc keymgmt/list.md; encedo-hem-python-api keymgmt.py (OQ-17 pagination fact); HEM-SDK-4/6 (requirements/start_point/encedo-pkcs11/REQUIREMENTS-hem.md); approved 2026-07-16
depends_on: ["REQ-AUTH-003", "REQ-API-003", "REQ-API-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/keymgmt/list — paginated key inventory

The SDK SHALL provide a key-inventory binding over
`GET /api/keymgmt/list[/{offset}[/{limit}]]` (scope `keymgmt:list`):

1. `ehem_key_list(ctx, offset, limit, &out)` — one page, parsed into a
   caller-owned page struct `{offset, total, listed, entries[]}`; each
   entry carries `kid` (32-char hex string, wire format preserved),
   `type` (algorithm string as sent by the device), `label`, optional
   `descr` (base64-decoded bytes; the device omits the field when empty),
   and `created`/`updated` (unix timestamps). Tolerant parsing per
   ARCHITECTURE §6.
2. `ehem_key_list_all(ctx, &out)` — walks the whole repository and returns
   one merged array. Termination condition: `offset ≥ total` (or
   `listed == 0`) — **never** `listed < limit`, because the device
   hard-caps `limit` at 15 server-side regardless of the requested value
   (doc; python OQ-17 marks `listed < limit` unreliable on observed
   firmware).

**Rationale:** Key inventory is the M3 foundation: `hem-tool keys list` /
`keys rm` (goal.txt tool milestone) and the pkcs11 object table build on
it. Listing returns *every* key including the device's own TLS material
and paired-authenticator keys — filtering/protection is client-side
(REQ-TOOL-005). Noted gap for M4: HEM-SDK-4 expects an "allowed modes
(ExDSA/ECDH/both)" attribute per key, but neither the list nor the get
response carries it per the doc — how mode metadata is obtained must be
resolved at M4 decomposition (candidate: §12 risk 3 device experiments).

**Acceptance criteria:**
- [x] Page call parses full and minimal fixtures (absent `descr` → NULL/0
      length); missing `kid`/`type` in an entry → `EHEM_ERR_PROTOCOL`;
      unknown fields ignored; `_free` NULL-safe; ASan/LSan clean
      (fake-transport unit tests). — tests/unit/test_keymgmt.c
- [x] Full walk paginates using `offset ≥ total` / `listed == 0`; a fixture
      sequence where `listed < requested limit` mid-walk does NOT terminate
      the walk early (unit test). — test_list_all_multipage_walk (page 2
      returns listed 8 < limit 10, walk continues to offset ≥ total).
- [x] Declares scope `keymgmt:list`; 401/403 map per REQ-AUTH-003; 406/409
      → `EHEM_ERR_DEVICE` with device payload in `ehem_last_error`
      (unit tests). — test_list_401_reacquire_then_success,
      test_list_403_scope, test_list_406_device, test_list_409_device.
- [x] Live: full walk against the dev device returns its key population
      (≥ the protected TLS pair) with kid/type/label populated
      (integration test). — tests/integration/test_keymgmt_live.c: 6 keys
      including "TLS PrivateKey"/"TLS Certificate", kid/type populated.
