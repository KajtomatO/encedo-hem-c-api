---
id: REQ-KEY-005
title: Binding for /api/keymgmt/create — generate a key on the device
status: verified
priority: must
revision: 1
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
- `label` — pre-validated printable and **≤ 31 bytes**. Sources conflict:
  the doc says `isvalid_label(…, STOREDKEY_LABEL_MAX_LENGTH)` "≤32
  bytes", the python client enforces ≤31 (working live). The stricter
  bound applies until the 32-byte case is live-probed.
- optional `mode` — one of the literals `ECDH` | `ExDSA` | `ECDH,ExDSA`;
  only meaningful for NIST-P ECC. **Device default is ECDH-only**, so a
  NIST-P key intended for signing must explicitly pass a mode containing
  `ExDSA` (python OQ-19; sign otherwise fails 406). The SDK passes the
  caller's choice through verbatim and imposes no default.
- optional `descr` — raw bytes, SDK base64-encodes. Length cap sources
  conflict: ARCHITECTURE §2 says ≤ 64 bytes, the python client enforces
  ≤ 128 (working live). The SDK passes through without a client-side cap
  until probed; device 400/406 reports the real limit.

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
- [x] Label over 31 bytes or non-printable → `EHEM_ERR_ARG` without any
      transport call; device 400 (unsupported type/invalid mode) →
      `EHEM_ERR_DEVICE` with payload; 406 (repo full) → `EHEM_ERR_DEVICE`
      (unit tests). — test_create_label_too_long / _nonprintable (0 requests),
      test_create_400_device, test_create_406_device.
- [x] Declares scope `keymgmt:gen` (unit test). — test_create_minimal decodes
      the token-acquisition eJWT and asserts `"scope":"keymgmt:gen"`.
- [x] Live: create an `EHEMTEST`-labeled ED25519 key with a descr →
      returned kid appears in list with that label/descr (integration
      test; cleanup per REQ-TEST-003). — tests/integration/test_keymgmt_mutate_live.c.
- [ ] OPEN (live probe, may resolve in M5): actual label max (31 vs 32)
      and descr max (64 vs 128) — record device behavior here. — NOT probed
      in M3 (the live test used a ~19-char label and a 6-byte descr, both well
      within bounds). SDK still enforces the stricter ≤31 label; boundary probe
      deferred to M5 per this criterion.
