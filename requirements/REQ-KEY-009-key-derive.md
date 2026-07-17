---
id: REQ-KEY-009
title: Binding for /api/keymgmt/derive — ECDH+HKDF derived key stored on device
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M7: keymgmt derive); encedo-hem-api-doc keymgmt/derive.md + discrepancies/DISCREPANCIES-OFFICIAL-DOCS.md §"derive vs ecdh"; encedo_firmware api_keymgmt.c:1285 api_post_keymgmt_derive (fw v1.2.2 — NOT prototyped in api.h; hem-api-tester test_10.php runs it green on real hardware); encedo-hem-python-api keymgmt.py derive (OQ-23: keymgmt:gen scope reuse); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-KEY-005", "REQ-OPS-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Binding for /api/keymgmt/derive — ECDH+HKDF derived key stored on device

The SDK SHALL provide `ehem_key_derive(ctx, kid, label, type, ext_kid,
pubkey, pubkey_len, mode, descr, descr_len, out_kid)` over
`POST /api/keymgmt/derive`, returning the derived key's KID from the
`{"kid"}` response.

- **Pipeline (firmware):** shared = ECDH(`kid` private, peer) via
  CRYPTO_DeriveKey → HKDF-SHA256(shared, salt=∅, info =
  `"encedo-<type>"`, api_keymgmt.c:20-36 — the exact context strings the
  doc tabulates) → REPO_GenKey_* with the output as seed. The derived
  private key never leaves the device.
- Peer = **exactly one** of `ext_kid` (repo key, e.g. from REQ-KEY-008)
  / `pubkey` (raw base64) — same rule and validation helper family as
  REQ-OPS-004.
- `type` is the **derived** key's family: the 17 non-PQC create types
  (NIST×4, CURVE25519/448, ED25519/448, SHA2-256/384/512,
  SHA3-256/384/512, AES128/192/256). PQC types are not derivable
  (handler has no MLKEM/MLDSA branches; doc agrees).
- `mode` optional, NIST-only literals as create (`ECDH` / `ECDH,ExDSA` /
  `ExDSA`); the SDK applies REQ-TOOL-009's tool-side NIST default only
  in hem-tool, never in the binding (verbatim pass-through).
- **Scope:** the handler accepts `keymgmt:derive` OR `keymgmt:gen`
  (api_keymgmt.c:1329). The SDK requests **`keymgmt:gen`** — proven live
  by the hem-api-tester across create/derive/import and shared with
  REQ-KEY-005's cached token (python OQ-23 rationale adopted).
- Pre-validation → `EHEM_ERR_ARG`, no I/O: kid not 32 hex; label rules;
  unknown-length type (> 16 chars); both-or-neither peer; descr_len >
  64. Errors: 406 (ECDH/HKDF/repo failure — includes non-ECDH source
  key, wrong-curve peer) → `EHEM_ERR_DEVICE` with detail; 400 →
  `EHEM_ERR_DEVICE`; 403 → `EHEM_ERR_SCOPE_DENIED`.

**Rationale:** M7 milestone item; the Manager relies exclusively on
derive (not raw ecdh) for key agreement — two parties that share pubkeys
and a `type` deterministically converge on the same key, giving
pair-wise symmetric channels without transporting key material.
**Firmware quirk to arbitrate live:** the handler feeds wc_HKDF an input
length equal to the TARGET key length, not the actual ECDH secret length
(`wc_HKDF(WC_SHA256, tmpbuf, outlen, …)` with `outlen` already
overwritten by the target size, api_keymgmt.c:1465-1600) — when the
ECDH secret is shorter than the target (e.g. X25519 32 B → SECP521R1
66 B) the HKDF input includes stale stack bytes, potentially making the
doc's determinism claim false for those combos.

**Acceptance criteria:**
- [ ] Unit (fake transport): body carries `{kid, label, type}` + exactly
      one peer field + `mode`/`descr` only when given; `{"kid"}` parsed;
      error mapping; `EHEM_ERR_ARG` pre-validation (incl. both-peers)
      with zero transport calls.
- [ ] Live (byte-exact cross-check): device X25519 key + local
      shim-generated peer → derive `SHA2-256`; locally compute
      HKDF-SHA256(shim ECDH secret, info="encedo-sha256") and HMAC a
      message with it; device `/api/crypto/hmac/hash` on the derived KID
      returns the identical MAC (proves the whole documented pipeline —
      raw-secret input, info string, seed semantics). Cleanup per
      REQ-TEST-003.
- [ ] Live (determinism, equal-length combo): derive ED25519 twice from
      identical inputs → `ehem_key_get` pubkeys are identical.
- [ ] OPEN (live probe): determinism for secret < target — derive
      SECP521R1 twice from an X25519 source with the same peer; equal
      pubkeys ⇒ the stale-stack read is benign in practice, differing
      pubkeys ⇒ non-deterministic (doc conflict recorded either way; SDK
      header documents the safe combos).
- [ ] OPEN (live probe): exact scope `keymgmt:derive` accepted or not on
      fw v1.2.2 (the api.h prototype is missing — confirm routing and
      the alternative scope both behave; SDK keeps `keymgmt:gen`).
