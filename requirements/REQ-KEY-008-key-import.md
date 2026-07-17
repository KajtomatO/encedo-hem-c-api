---
id: REQ-KEY-008
title: Binding for /api/keymgmt/import — import an external public key
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M7: keymgmt import); encedo-hem-api-doc keymgmt/import.md; encedo_firmware api_keymgmt.c:1123 api_post_keymgmt_import (fw v1.2.2; REPO_ImportKey body not in the source checkout); encedo-hem-python-api keymgmt.py import_key (406 = dedup finding); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-KEY-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#2-context--constraints"]
---

# Binding for /api/keymgmt/import — import an external public key

The SDK SHALL provide `ehem_key_import(ctx, type, label, pubkey,
pubkey_len, mode, descr, descr_len, out_kid)` over
`POST /api/keymgmt/import`, returning the new key's KID from the
`{"kid"}` response.

- **Public keys only** — the endpoint imports the peer half for
  ext_kid-based ECDH/verify/wrap flows; there is no private-key import
  (doc + handler agree).
- `type` passed verbatim; the doc lists 14 values (`SECP256R1/384R1/
  521R1/256K1`, `CURVE25519/448`, `ED25519/448`, `MLKEM512/768/1024`,
  `MLDSA44/65/87`); symmetric types are rejected by design.
- `mode` optional, exact literals `ECDH` / `ECDH,ExDSA` / `ExDSA`
  (handler strcmp, api_keymgmt.c:1207-1219; anything else → 400); NIST
  semantics per REQ-TOOL-009's mode notes.
- Pre-validation → `EHEM_ERR_ARG`, no I/O: NULL/empty type or label;
  label > 32 printable; NULL/empty pubkey; descr_len > 64. pubkey sent
  base64; **no client-side pubkey length cap** — see the dead-check
  finding below.
- Errors: 406 (repo rejected — **known cause: the public key already
  exists, key dedup**, python-client finding) → `EHEM_ERR_DEVICE` with
  detail; 400 → `EHEM_ERR_DEVICE`; 403 → `EHEM_ERR_SCOPE_DENIED`.
- Scope `keymgmt:imp`, sub != M.

**Rationale:** M7 milestone item; feeds `ext_kid` peers to REQ-OPS-004
(ecdh), REQ-OPS-003 (verify) and REQ-OPS-009 (wrap). **Doc/firmware
CONFLICT to arbitrate live:** the handler validates the pubkey with
`isvalid_base64(pubkey_base64, 66+4)` — nominally capping decoded length
at 70 bytes, which would make the doc's ML-KEM/ML-DSA rows impossible
(pubkeys 800-2592 B) — but that length check is **dead code** (misc.c
isvalid_base64 tests length after its loop decremented the counter to
−1; same bug REQ-KEY-005 pinned for DESCR). Which types actually import
is decided by `REPO_ImportKey`, whose source is not in the checkout —
the device arbitrates.

**Acceptance criteria:**
- [ ] Unit (fake transport): body carries `{type, label, pubkey(b64)}` +
      `mode`/`descr` only when given (bytes asserted); `{"kid"}` parsed
      to out_kid; 400/403/406 mapping; `EHEM_ERR_ARG` pre-validation
      with zero transport calls.
- [ ] Live: import a shim-generated X25519 public key (EHEMTEST label) →
      `ehem_ecdh` via `ext_kid` against a device key matches the
      shim-computed shared secret byte-exact; list shows the imported
      key's flag-set (recorded for REQ-KEY-006 vocabulary); delete.
- [ ] Live: re-import of the identical pubkey → 406 (dedup) confirmed
      and recorded.
- [ ] OPEN (live probe): which `type` families import on fw v1.2.2 —
      probe at least SECP256R1 (compressed x963, as `ehem_key_get`
      exports), ED25519, and MLKEM512 (800-byte pubkey, testing the dead
      70-byte cap); record accepted/rejected per family and the expected
      pubkey encoding for NIST curves.
