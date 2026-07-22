---
id: REQ-KEY-007
title: Binding for /api/keymgmt/update — rewrite LABEL/DESCR by KID
status: verified
priority: must
revision: 2
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
  optional.
- **Whole-record rewrite (rev2 — live finding 2026-07-18, device > doc):**
  the firmware rewrites the key's metadata record on every update, so an
  OMITTED `descr` CLEARS the stored DESCR — it is NOT "left untouched" as
  the doc's Notes claim (update.md is wrong; upstream-doc filing
  candidate). The SDK passes the semantics through verbatim and documents
  that keep-descr requires read-then-resend; the live test pins the
  clearing behavior.
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
- [x] Unit (fake transport): body carries exactly `{kid, label}` +
      `descr` only when given (base64, asserted bytes); empty-200 →
      `EHEM_OK`; 406→NOT_FOUND / 400→DEVICE / 403→SCOPE_DENIED mapping;
      `EHEM_ERR_ARG` pre-validation with zero transport calls.
- [x] Live (EHEMTEST key, 2026-07-18): create → update label+descr →
      list shows both, search by the new DESCR prefix finds it; a
      label-only update then CLEARS the descr (whole-record rewrite —
      pinned in test_update_import_live); delete clean.
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18):** unknown-but-well-formed
      KID → HTTP 406 → `EHEM_ERR_NOT_FOUND` (doc confirmed).
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18):** descr-only body (no
      label, internal request path) → HTTP 400 — the label-required
      firmware rule the SDK mirrors is confirmed.
