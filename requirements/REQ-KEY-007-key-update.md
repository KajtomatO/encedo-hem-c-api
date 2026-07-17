---
id: REQ-KEY-007
title: Binding for /api/keymgmt/update — rewrite LABEL/DESCR by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M7: keymgmt update — LABEL/DESCR); encedo-hem-api-doc keymgmt/update.md; encedo_firmware api_keymgmt.c:999 api_post_keymgmt_update (fw v1.2.2); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-KEY-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#2-context--constraints"]
---

# Binding for /api/keymgmt/update — rewrite LABEL/DESCR by KID

The SDK SHALL provide `ehem_key_update(ctx, kid, label, descr,
descr_len)` over `POST /api/keymgmt/update`, treating an empty-2xx
response as success.

- **`label` is REQUIRED** by the firmware parse (`!label` → 400) even
  though a dead fallback branch suggests label-or-descr; the doc records
  the same (update.md). The SDK mirrors: `label` mandatory, `descr`
  optional (NULL = leave the stored DESCR unchanged).
- Pre-validation → `EHEM_ERR_ARG`, no I/O: kid not 32 hex; label NULL,
  empty, > 32 printable chars (REQ-KEY-005 limits); descr_len > 64.
  `descr` is sent base64-encoded (raw bytes in, like create).
- Scope `keymgmt:upd` (prefix match device-side), sub != M; same cached
  token discipline as list/search.
- Errors: 406 (repo update failed — unknown/deleted KID per doc) →
  `EHEM_ERR_NOT_FOUND` with detail; 400 → `EHEM_ERR_DEVICE`; 403 →
  `EHEM_ERR_SCOPE_DENIED`.
- Audit side effect (documented, not asserted):
  `LOG_TYPE_KEY_ATTRIBUTES_CHANGED`.

**Rationale:** M7 milestone item "keymgmt import and update
(LABEL/DESCR)" — the consumer contract's mutable-DESCR use (HEM-SDK-4
DESCR-prefix search implies writable DESCR). The firmware updates
whichever fields are sent and leaves omitted ones untouched.

**Acceptance criteria:**
- [ ] Unit (fake transport): body carries exactly `{kid, label}` +
      `descr` only when given (base64, asserted bytes); empty-200 →
      `EHEM_OK`; 406→NOT_FOUND / 400→DEVICE / 403→SCOPE_DENIED mapping;
      `EHEM_ERR_ARG` pre-validation with zero transport calls.
- [ ] Live (EHEMTEST key): create → update label → get shows new label;
      update label+descr → search by new DESCR prefix finds it; delete.
- [ ] OPEN (live probe): 406-vs-other for an unknown-but-well-formed KID
      confirmed (doc says 406 = not found; device arbitrates the
      NOT_FOUND mapping).
- [ ] OPEN (live probe): descr-only update (label omitted) → 400
      confirmed, pinning the label-required firmware behavior the SDK
      mirrors.
