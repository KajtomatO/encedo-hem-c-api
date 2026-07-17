---
id: REQ-OPS-009
title: Bindings for /api/crypto/cipher/wrap and /api/crypto/cipher/unwrap — AES key wrap by KID
status: approved
priority: must
revision: 1
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
- [ ] Unit (fake transport): wrap body carries `{kid, msg(b64)}` +
      `alg`/`ext_kid`/`pubkey`/`ctx`/`iv` only when given; unwrap
      mirrors; `{"wrapped"}` and the unwrap response decoded into
      caller-owned buffers (unwrapped zeroized on free); error mapping
      and `EHEM_ERR_ARG` pre-validation with zero transport calls.
- [ ] Live round-trip (EHEMTEST AES-256 key): wrap 32 bytes → wrapped is
      40 bytes → unwrap returns the original; tampered wrapped → 406.
- [ ] Live local cross-check: device wrap output equals local
      wolfCrypt `wc_AesKeyWrap` under the same KEK bytes (KEK derived
      via the REQ-OPS-009 ECDH flow against a shim-known peer so the
      test possesses the KEK; proves RFC 3394 + KEK derivation
      end-to-end).
- [ ] OPEN (live probe): CRYPTO_Wrap's HKDF info string — `"encedo"`
      (handler comment/doc) vs `"encedo-aes"` ‖ ctx (REQ-OPS-006's
      encrypt finding) vs other; byte-exact arbitration recorded here
      and in the header doc.
- [ ] OPEN (live probe): msg alignment — wrap of a non-8-multiple and a
      short (8-byte) msg; response field name and status for unwrap
      failures recorded (unwrap handler tail not yet read; doc says 406).
- [ ] OPEN (live probe): unwrap response field name + whether width
      follows encrypt's ≤-stored-key rule (AES256 KEK serving AES128
      wrap).
