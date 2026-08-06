---
id: REQ-KEY-008
title: Binding for /api/keymgmt/import — import an external public key
status: verified
priority: must
revision: 4
source: ARCHITECTURE.md §11 (M7: keymgmt import); encedo-hem-api-doc keymgmt/import.md; encedo_firmware api_keymgmt.c:1123 api_post_keymgmt_import (fw v1.2.2; REPO_ImportKey body not in the source checkout); encedo-hem-python-api keymgmt.py import_key (406 = dedup finding); approved 2026-07-17; rev bump 2026-08-06 = M9-060 gate probe resolves the dedup-across-reboot criterion
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
- [x] Unit (fake transport): body carries `{type, label, pubkey(b64)}` +
      `mode`/`descr` only when given (bytes asserted); `{"kid"}` parsed
      to out_kid; 400/403/406 mapping; `EHEM_ERR_ARG` pre-validation
      with zero transport calls.
- [x] Live (2026-07-18): imported a shim-generated X25519 public key →
      readback via get is byte-identical → `ehem_ecdh` via `ext_kid`
      against a device key matched the shim-computed shared secret
      byte-exact; cleanup clean.
- [x] ~~OPEN~~ **RESOLVED (live 2026-07-18):** re-import of the identical
      pubkey → HTTP 406 with an EMPTY payload — the dedup rejection
      (python finding confirmed; the device gives no error body to
      distinguish dedup from other repo rejects).
- [x] ~~OPEN~~ **RESOLVED (M9-060 gate probe, 2026-08-06):** on the
      post-wipe HEALTHY repo the hypothesis does NOT hold — scripted
      probe (RFC 8032 §7.1 Ed25519 pubkey, deterministic): import →
      200; delete → 200; re-import SAME boot → 200; delete → reboot →
      check-in → re-import → **200** (and every import returned the
      SAME kid, so kids are content-derived). Verdict: the 2026-07-18
      dedup-across-reboot observations were a property of the
      DEBRIS-laden pre-wipe repo state (~1560 non-compacted deleted
      slots), not general firmware behavior. Dedup (406) applies only
      to material that currently EXISTS in the repo. Tests keep
      per-run-unique material as belt-and-braces against future repo
      degradation.
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18):** type support on fw
      v1.2.2-DIAG — SECP256R1 (SEC1 COMPRESSED point, 33 B, mode
      ECDH,ExDSA) ACCEPTED; ED25519 (raw 32 B) ACCEPTED; **MLKEM512 with
      an 800-byte pubkey ACCEPTED** — the nominal 70-byte decoded cap is
      confirmed dead code (no length rejection whatsoever; the repo did
      not validate the ML-KEM key material either — arbitrary bytes of
      the right shape stored fine). NIST encoding = the compressed x963
      form `ehem_key_get` exports.
