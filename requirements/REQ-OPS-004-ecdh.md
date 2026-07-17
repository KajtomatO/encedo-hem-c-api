---
id: REQ-OPS-004
title: Binding for /api/crypto/ecdh — raw ECDH shared secret by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M6); encedo-hem-api-doc crypto/ecdh.md; encedo_firmware api_crypto.c api_post_crypto_ecdh + crypto.c CRYPTO_ECDH/CRYPTO_DeriveKey (fw v1.2.2); HEM-SDK-7 (ECDH), HEM-OP-1; approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-KEY-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Binding for /api/crypto/ecdh — raw ECDH shared secret by KID

The SDK SHALL provide `ehem_ecdh(ctx, kid, ext_kid, pubkey, pubkey_len,
alg, &out)` over `POST /api/crypto/ecdh`, performing ECDH between the
device key `kid` and a peer — either `ext_kid` (a key already in the repo)
or `pubkey` (caller-supplied raw peer public key, base64-encoded by the
binding) — and returning the decoded `ecdh` bytes in a caller-owned,
zeroized-on-free struct.

- **Scope:** exact `keymgmt:use:<kid>` on the *primary* kid (sub != M);
  no scope is checked on `ext_kid` (firmware api_crypto.c:1819-1834).
  Same cached per-KID token as get/sign (REQ-AUTH-002).
- **Peer selection:** exactly one of `ext_kid` / `pubkey` (both or neither
  → `EHEM_ERR_ARG`, no I/O). `kid` must be ECDH-capable (mode `ECDH`);
  with `ext_kid` both keys must be the same family (firmware
  CRYPTO_DeriveKey type check, crypto.c:236) — violations are the device's
  406.
- **Peer public key format** (crypto.c CRYPTO_DeriveKey): NIST curves →
  **compressed x963**, exact length curve_size+1 (33/49/67/33 — the same
  format `ehem_key_get` returns, REQ-KEY-006, so get(peer)→ecdh(pubkey)
  composes); X25519/X448 → raw little-endian 32/56. Wrong length → 406
  (device-side); the SDK passes bytes through (max 67, longer →
  `EHEM_ERR_ARG`).
- **`alg`** (optional, passed verbatim, NULL omits): `SHA2-256`,
  `SHA2-384`, `SHA2-512`, `SHA3-256`, `SHA3-384`, `SHA3-512` — the device
  returns that hash of the shared secret instead of raw bytes.
- **Raw-mode truncation (doc/firmware CONFLICT, device arbitrates):**
  ecdh.md says raw mode returns the full curve-length secret (48 for
  P-384, 66 for P-521, 56 for X448); firmware `CRYPTO_ECDH` crypto.c:1679
  sets the output length to 32 **unconditionally** in the raw branch —
  secrets longer than 32 bytes are truncated. Resolution is the open
  criterion below; until then the SDK documents raw mode as
  "device-defined length, ≤ curve size" and returns whatever the device
  sent.
- **Errors:** 403 → `EHEM_ERR_SCOPE_DENIED`; 400/406 → `EHEM_ERR_DEVICE`
  with detail (406 covers not-found / not-ECDH-capable / family mismatch /
  crypto failure indistinguishably).

**Rationale:** M6 milestone core (ARCHITECTURE §11); the PKCS#11 consumer
derives shared secrets (HEM-SDK-7). The raw endpoint stores nothing —
distinct from `/api/keymgmt/derive` (M7 territory) which HKDFs into a new
stored key.

**Acceptance criteria:**
- [ ] Against the fake transport: body carries `{kid}` plus exactly one of
      `{ext_kid}` / `{pubkey(b64)}` and `alg` only when given; `ecdh` is
      decoded into a caller-owned buffer; `_free` NULL-safe and zeroizing;
      both-or-neither peer args and oversize pubkey → `EHEM_ERR_ARG` with
      zero transport calls; 403/406 mapping (unit tests).
- [ ] Live X25519 cross-check: a local X25519 keypair from the crypto shim
      (REQ-AUTH-001 primitives) sends its pubkey against an `EHEMTEST`
      X25519 device key; the returned raw secret equals the shim-computed
      `ehem_x25519_shared` against the device key's public key from
      `ehem_key_get` — byte-exact; the `SHA2-256` variant equals the local
      SHA-256 of that secret.
- [ ] Live ext_kid symmetry: two `EHEMTEST` same-family NIST keys A,B —
      `ecdh(A, ext_kid=B)` == `ecdh(B, ext_kid=A)`; a family-mismatched
      pair → 406/`EHEM_ERR_DEVICE`.
- [ ] OPEN (live probe, closes the doc/firmware conflict): raw-mode output
      length for a >256-bit family (P-384 or P-521) — 32 (firmware
      truncation) or full curve length (doc)? Record the result here and
      in the binding's doc comment.
