---
id: REQ-OPS-009
title: Bindings for /api/crypto/cipher/wrap and /api/crypto/cipher/unwrap — AES key wrap by KID
status: verified
priority: must
revision: 2
source: user decision 2026-07-17 (pulled into M7 from the M6 "(M7/M9 sweep)" note); encedo-hem-api-doc crypto/cipher-wrap.md, crypto/cipher-unwrap.md; encedo_firmware api_crypto.c:721 api_post_crypto_cipher_wrap / :947 api_post_crypto_cipher_unwrap + crypto.c CRYPTO_Wrap/CRYPTO_Unwrap (fw v1.2.2); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-OPS-004", "REQ-OPS-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/crypto/cipher/wrap and /api/crypto/cipher/unwrap — AES key wrap by KID

The SDK SHALL provide `ehem_wrap(ctx, kid, alg, msg, msg_len, ext_kid,
pubkey, pubkey_len, hkdf_ctx, hkdf_ctx_len, iv, iv_len, &out)` over
`POST /api/crypto/cipher/wrap` — returning caller-owned `{wrapped}` —
and its mirror `ehem_unwrap(...)` over `POST /api/crypto/cipher/unwrap`
returning the unwrapped secret (zeroized on free).

- **AES Key Wrap** (KEYWRAP_BLOCK_SIZE = 8; wrapped = msg + 8 bytes);
  `msg` is REQUIRED non-empty — the handler comment's "empty → generate"
  path is unreachable (same finding as REQ-OPS-002: missing/empty msg →
  400), ≤ 2048 bytes. RFC 3394 alignment rules (multiple-of-8, ≥ 16) are
  device-enforced — open criterion below.
- **`alg` optional**, exact literals `AES128` / `AES192` / `AES256`,
  device default **AES256** (api_crypto.c:820-835 — a real default,
  unlike encrypt's unreachable one; unknown literal → 400). NULL alg
  omits the field.
- **Two KEK flows** (mirroring REQ-OPS-006): direct (`kid` names an AES
  key) or ECDH-derived (exactly one of `ext_kid`/`pubkey`, optional
  `ctx` ≤ 64 bytes for HKDF). The handler comments claim HKDF default
  ctx `"encedo"`; REQ-OPS-006 proved encrypt's real prefix is
  `"encedo-aes"` ‖ ctx — CRYPTO_Wrap's actual info string is an open
  criterion (device arbitrates, byte-exact local probe).
- **`iv` optional** (wrap AND unwrap): omitted → the RFC 3394 default
  IV; the handler validates it against KEYWRAP_BLOCK_SIZE (8 bytes; the
  length check is the known-dead isvalid_base64 check — SDK caps at 8
  client-side).
- Pre-validation → `EHEM_ERR_ARG`, no I/O: kid not 32 hex; NULL/empty
  msg; msg_len > 2048; alg given but not one of the three literals;
  both `ext_kid` and `pubkey`; pubkey_len > 67; hkdf_ctx_len > 64;
  iv_len ≠ 8 when given.
- **Scope:** exact `keymgmt:use:<kid>` (strcmp, api_crypto.c:798-799 —
  same as sign/encrypt), sub != M, shared per-KID cached token. Errors:
  403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`; 406 (wrap
  oper/width/ECDH failure, unwrap integrity failure) →
  `EHEM_ERR_DEVICE` with detail.

**Rationale:** completes the cipher group (REQ-OPS-006 covered
encrypt/decrypt); wrap is the doc-blessed way to protect key material
under a device-held KEK, and the import.md flow names wrap's recipient
use ("as the recipient in cipher-wrap"). Pulled into M7 while the M6
cipher/peer-arg helpers are fresh (user decision 2026-07-17).

**Acceptance criteria:**
- [x] Unit (fake transport): wrap body carries `{kid, msg(b64)}` +
      `alg`/`ext_kid`/`pubkey`/`ctx`/`iv` only when given; unwrap
      mirrors; `{"wrapped"}` and the unwrap response decoded into
      caller-owned buffers (unwrapped zeroized on free); error mapping
      and `EHEM_ERR_ARG` pre-validation with zero transport calls.
- [x] Live round-trip (EHEMTEST AES-256 key, 2026-07-18): wrap 32 bytes
      → 40 → unwrap returned the original; one flipped byte → 406.
- [x] Live local cross-check (2026-07-18): device ECDH-KEK wrap output
      is BYTE-IDENTICAL to local `wc_AesKeyWrap` under
      HKDF-SHA256(shim X25519 secret, salt=∅, info) — RFC 3394 + KEK
      derivation proven end-to-end and externally reproducible (unlike
      keymgmt derive, this HKDF uses the secret's real length).
- [x] ~~OPEN~~ **RESOLVED (source + live byte-exact, 2026-07-18):** the
      HKDF info prefix is **`"encedo-kek"` ‖ ctx-bytes** (firmware
      crypto.c:57 CRYPTO_HKDF_CONTEXT_KEK) — a THIRD literal: the
      doc/handler-comment's `"encedo"` is wrong, and it is distinct
      from encrypt's `"encedo-aes"`. Recorded in the header doc; the
      live cross-check pins it. Upstream doc filing candidate.
- [x] ~~OPEN~~ **RESOLVED (live 2026-07-18):** alignment is
      device-enforced — 20-byte msg (not %8) → 406 (fw −20); 8-byte msg
      (single semiblock) → 406 (wolfCrypt RFC 3394 two-semiblock
      minimum). SDK keeps alignment device-side, per the rev1 decision.
- [x] ~~OPEN~~ **RESOLVED (source + live, 2026-07-18):** unwrap response
      field is `"unwrapped"` (api_crypto.c); width follows encrypt's
      ≤-stored-key rule — the AES-256 key served an AES128 KEK wrap
      (24-byte blob) live.
