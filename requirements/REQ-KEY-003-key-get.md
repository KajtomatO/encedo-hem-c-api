---
id: REQ-KEY-003
title: Binding for /api/keymgmt/get — public material and metadata by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6; encedo-hem-api-doc keymgmt/get.md; encedo-hem-python-api keymgmt.py (OQ-16 scope finding on fw v1.2.2-DIAG); HEM-SDK-5 (public-key read); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-AUTH-002", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/keymgmt/get — public material and metadata by KID

The SDK SHALL provide `ehem_key_get(ctx, kid, &out)` over
`GET /api/keymgmt/get/{kid}`, returning a caller-owned struct in which,
per the doc's response shapes, `type` and `updated` are always present and
at most one material field is set: `pubkey` (asymmetric types; decoded
raw bytes in the algorithm-native wire encoding) or `der` (types `CERT` /
`DER_PKEY`; decoded DER bytes) — symmetric types (AES/HMAC) return no
material at all. `descr` is optional (absent when empty). `label` is
**never** returned by this endpoint (the firmware deliberately omits it) —
labels come from list/search (REQ-KEY-001/002).

**Scope — doc/device conflict:** the doc accepts prefix `keymgmt:get`,
prefix `keymgmt:gen`, or exact `keymgmt:use:<kid>`. On fw v1.2.2-DIAG the
python client found only `keymgmt:use:<kid>` accepted (OQ-16; 403
otherwise). Precedence device > doc: the binding requests scope
`keymgmt:use:<kid-hex>` per call; the scope-keyed token cache
(REQ-AUTH-002) turns this into one cache entry per KID, acceptable at
tens of keys. This is the first concrete per-KID-scope fact for §12
risk 3 — record the live result there at implementation.

`kid` is pre-validated client-side (exactly 32 hex chars, else
`EHEM_ERR_ARG` with no network I/O); device 406 (kid not found) maps to
`EHEM_ERR_NOT_FOUND`.

**Rationale:** public-key read by KID is HEM-SDK-5 (serves
`CKA_VALUE`/`CKA_EC_POINT` queries); M3 binds it alongside the rest of
the keymgmt group per the milestone definition, M4 builds signing on it.

**Acceptance criteria:**
- [ ] Parses all four documented response shapes (asymmetric / CERT /
      DER_PKEY / symmetric) into the struct with correct absent-field
      semantics; unknown fields ignored; `_free` NULL-safe; ASan/LSan
      clean (fake-transport unit tests).
- [ ] Requests exact scope `keymgmt:use:<kid>`; two gets of different kids
      acquire two cache entries, a repeat get reuses its entry (unit test
      asserting token-request count).
- [ ] Malformed kid → `EHEM_ERR_ARG` without any transport call; 406 →
      `EHEM_ERR_NOT_FOUND`; 403 → `EHEM_ERR_SCOPE_DENIED` (unit tests).
- [ ] Live: get of a created `EHEMTEST` ED25519 key returns a 32-byte
      pubkey and its descr (integration test).
- [ ] OPEN (live probe): does current firmware accept the documented
      `keymgmt:get` / `keymgmt:gen` scopes for this endpoint, or still only
      `keymgmt:use:<kid>`? Record the observation here and in §12 risk 3.
