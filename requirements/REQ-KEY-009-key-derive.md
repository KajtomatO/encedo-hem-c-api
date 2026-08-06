---
id: REQ-KEY-009
title: Binding for /api/keymgmt/derive — ECDH+HKDF derived key stored on device
status: verified
priority: must
revision: 2
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
- [x] Unit (fake transport): body carries `{kid, label, type}` + exactly
      one peer field + `mode`/`descr` only when given; `{"kid"}` parsed;
      error mapping; `EHEM_ERR_ARG` pre-validation (incl. both-peers)
      with zero transport calls.
- [x] ~~byte-exact~~ **RESOLVED AS A CONFLICT (live 2026-07-18, rev2):**
      the documented pipeline is NOT externally reproducible. The device's
      derived SHA2-256 key produced a MAC matching NONE of 85 candidate
      local reconstructions (incl. the doc-exact RFC 5869 HKDF over the
      raw wolfCrypt X25519 secret that CRYPTO_DeriveKey provably outputs —
      crypto.c:363 wc_curve25519_shared_secret_ex, source verified). The
      repo's key-generation step (`REPO_GenKey_*`, source ABSENT from the
      firmware checkout — repo.c defines no REPO_* functions) applies an
      undisclosed extra transformation to the seed. Consequence recorded
      in the header doc: derive is device-side key agreement only; the
      doc's "two parties converge" holds at most between HEM devices, not
      for external implementations. Pinned by assert_memory_not_equal in
      test_derive_live (a doc-conformant firmware change would surface as
      a test failure). Upstream doc filing candidate.
- [x] Live (determinism, equal-length combo, 2026-07-18): derive ED25519
      twice from identical inputs → the repo DEDUP-rejected the second
      derive with 406 — identical material, determinism proven (and a new
      fact: derived keys join the same dedup domain as imports).
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18):** secret < target is
      REJECTED device-side — SECP521R1 from an X25519 source → 406 on the
      first derive; the stale-stack HKDF read is unreachable in practice
      for cross-family derives. (Same-family long targets, e.g. P-521
      source → P-521 derive, remain untested — the source secret is 66 B
      there, so no stale read occurs anyway.)
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18):** the exact scope
      `keymgmt:derive` IS accepted (200) on fw v1.2.2-DIAG, matching
      api_keymgmt.c:1329; the SDK keeps `keymgmt:gen` for token sharing
      with create/import.
