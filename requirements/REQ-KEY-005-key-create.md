---
id: REQ-KEY-005
title: Binding for /api/keymgmt/create — generate a key on the device
status: verified
priority: must
revision: 2
source: ARCHITECTURE.md §6, §11 (M3 integration tests create EHEMTEST keys); encedo-hem-api-doc keymgmt/create.md; encedo-hem-python-api keymgmt.py (label ≤31, NIST-ECC mode default, descr cap); HEM-SDK-6 (generate); approved 2026-07-16
depends_on: ["REQ-AUTH-003", "REQ-API-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/keymgmt/create — generate a key on the device

The SDK SHALL provide `ehem_key_create(ctx, params, &kid_out)` over
`POST /api/keymgmt/create` (scope `keymgmt:gen`), where params carry:

- `type` — the doc's literal vocabulary (`SECP256R1/384R1/521R1/256K1`,
  `CURVE25519/448`, `ED25519/448`, HMAC families `SHA2-256…SHA3-512`,
  `AES128/192/256`, `MLKEM512/768/1024`, `MLDSA44/65/87`; note AES has no
  dash and HMAC uses the raw hash name). Passed through as given — the
  SDK does not maintain its own allowlist (device firmware may extend
  the set; unsupported → device 400 with payload).
- `label` — pre-validated printable and **≤ 32 bytes**. The conflict
  between the doc (`isvalid_label(…, STOREDKEY_LABEL_MAX_LENGTH)` = 32)
  and the python client (≤31) is **RESOLVED** in favor of the doc: the
  device max is 32 (firmware repo.h:64 `STOREDKEY_LABEL_MAX_LENGTH 32`;
  live probe 2026-07-16 — a 32-byte label is accepted, 33 → 400). The
  python client's ≤31 is over-strict.
- optional `mode` — one of the literals `ECDH` | `ExDSA` | `ECDH,ExDSA`;
  only meaningful for NIST-P ECC. **Device default is ECDH-only**, so a
  NIST-P key intended for signing must explicitly pass a mode containing
  `ExDSA` (python OQ-19; sign otherwise fails 406). The SDK passes the
  caller's choice through verbatim and imposes no default.
- optional `descr` — raw bytes, SDK base64-encodes, **capped at 64 bytes**
  client-side (`EHEM_ERR_ARG`). The conflict (ARCHITECTURE §2 ≤64 vs the
  python client's ≤128) is **RESOLVED** in favor of 64: the device stores
  the field with `STOREDKEY_DESCR_MAX_LENGTH == 64` (firmware repo.h:65)
  and reads it back into a fixed 64-byte buffer with a clamped copy, so a
  longer descr is **silently truncated on read**. The device's *create*
  side does not reject over-long descr — its `isvalid_base64` length check
  is broken (it tests the base64 length after the validation loop has
  already decremented the counter to −1; misc.c:577/597), so a 65-byte
  descr is accepted on create (live probe 2026-07-16) but unreadable in
  full. The SDK caps at 64 to refuse silent-data-loss keys.

On 200 the new `kid` (32-char hex) is returned to the caller.

**Rationale:** M3's milestone definition requires integration tests that
*create and delete* `EHEMTEST` keys end-to-end, and `keys rm` needs
disposable live targets — so the create binding lands in M3. M3 verifies
it live for at least ED25519; the per-family generation matrix (every
family created, used, deleted) plus hardware `random` remain M5 (its
placeholder stands).

**Acceptance criteria:**
- [x] Request body carries exactly the given `{type, label[, mode][,
      descr(b64)]}`; kid parsed from the reply (fake-transport unit tests
      asserting body bytes). — test_create_minimal, test_create_full_body
      (byte-exact `{"type":...,"label":...,"mode":...,"descr":"AQID"}`).
- [x] Label over 32 bytes, descr over 64 bytes, or a non-printable label
      → `EHEM_ERR_ARG` without any transport call; a 32-byte label and a
      64-byte descr are accepted and reach the device; device 400
      (unsupported type/invalid mode) → `EHEM_ERR_DEVICE` with payload;
      406 (repo full) → `EHEM_ERR_DEVICE` (unit tests). —
      test_create_label_too_long (33) / _descr_too_long (65) /
      _nonprintable (0 requests), test_create_label_max_ok (32) /
      _descr_max_ok (64) assert the body reaches the device,
      test_create_400_device, test_create_406_device.
- [x] Declares scope `keymgmt:gen` (unit test). — test_create_minimal decodes
      the token-acquisition eJWT and asserts `"scope":"keymgmt:gen"`.
- [x] Live: create an `EHEMTEST`-labeled ED25519 key with a descr →
      returned kid appears in list with that label/descr (integration
      test; cleanup per REQ-TEST-003). — tests/integration/test_keymgmt_mutate_live.c.
- [x] RESOLVED (live probe, STEP-M5-010, 2026-07-16): actual label max is
      **32** (32 accepted, 33 → device 400) and descr max is **64** (both
      64 and 65 accepted on create — the device's create-side length check
      is broken — but the get-side readback buffer is 64 bytes with a
      clamped copy, so >64 truncates silently). SDK now enforces label ≤32
      and descr ≤64 client-side (both `EHEM_ERR_ARG`). Probe via the
      internal request path to bypass client validation; keys deleted
      immediately (EHEMTEST prefix, REQ-TEST-003).
