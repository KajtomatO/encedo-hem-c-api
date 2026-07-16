---
id: REQ-KEY-004
title: Binding for /api/keymgmt/delete — remove a key by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M3); encedo-hem-api-doc keymgmt/delete.md; HEM-SDK-6 (delete by KID); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/keymgmt/delete — remove a key by KID

The SDK SHALL provide `ehem_key_delete(ctx, kid)` over
`DELETE /api/keymgmt/delete/{kid}` (scope `keymgmt:del`): `kid`
pre-validated client-side (exactly 32 hex chars, else `EHEM_ERR_ARG`
with no network I/O); success is HTTP 200 with an **empty body** (doc),
which the binding treats as `EHEM_OK` — same empty-2xx handling the
reboot binding established over the shared request path; device 406
(kid not found) maps to `EHEM_ERR_NOT_FOUND`.

Deletion is immediate and irreversible at the API level (the doc
documents no confirmation step; key material is zeroed in flash). The
SDK stays policy-free by design: every confirmation and protected-key
safeguard lives in hem-tool (REQ-TOOL-005/006), because ARCHITECTURE §8
defines protection as a client-side convention over labels.

**Rationale:** key removal is half of the goal.txt tool milestone and of
HEM-SDK-6 (pkcs11 `hem_destroy_object`). The `EHEMTEST` integration
policy (REQ-TEST-003) also needs it for cleanup.

**Acceptance criteria:**
- [ ] Sends method DELETE with the kid in the path and no body; empty-200
      → `EHEM_OK` (fake-transport unit tests).
- [ ] Malformed kid → `EHEM_ERR_ARG` without any transport call; 406 →
      `EHEM_ERR_NOT_FOUND`; 401/403 map per REQ-AUTH-003 (unit tests).
- [ ] Declares scope `keymgmt:del` (unit test).
- [ ] Live: a created `EHEMTEST` key is deleted; a follow-up
      get/list confirms it is gone; deleting the same kid again returns
      `EHEM_ERR_NOT_FOUND` (integration test).
