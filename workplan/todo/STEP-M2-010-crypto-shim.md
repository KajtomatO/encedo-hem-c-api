---
id: STEP-M2-010
title: "Crypto shim: wolfCrypt PBKDF2-SHA256 / HMAC-SHA256 / X25519 + RFC vectors"
milestone: M2
implements: ["REQ-AUTH-001"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `src/crypto_shim.{h,c}` wrapping wolfCrypt behind a minimal
internal API: `ehem_kdf_pbkdf2_sha256` (600 000 iters, 32-byte out),
`ehem_hmac_sha256`, `ehem_x25519_keypair_from_seed` (32-byte seed →
private clamp + public), `ehem_x25519_shared`, and `ehem_zeroize`
(non-elidable memset). wolfCrypt (wolfssl) becomes a build dependency
(find_package / FetchContent fallback, MinGW-compatible — §12 risk 5);
no wolfSSL type or header appears in public headers (same rule as curl,
enforced by the existing header check pattern).

**Notes:** KDF pinned to the python client per REQ-AUTH-001 (PBKDF2, NOT
the Manager's Argon2 — see ARCHITECTURE §12 risk 2). Vendored Argon2 was
dropped from M2. Mind wolfSSL licensing (§12 risk 1) — dependency, not
vendored code. Watch out: X25519 public keys are exchanged in standard
base64; wolfCrypt's curve25519 functions may need
`EC25519_LITTLE_ENDIAN` ordering flags to match RFC 7748 byte order.

**Definition of done**
- [ ] Shim compiles into the library on GCC + Clang under `-Werror`; no
      wolfSSL symbol/header leaks into `include/ehem/` (header-check test
      extended).
- [ ] Unit tests pass against published vectors: RFC 7748 §5.2 X25519
      (scalar mult + Diffie-Hellman §6.1), RFC 4231 HMAC-SHA256 cases,
      and a PBKDF2-HMAC-SHA256 vector; `verifies:` tags reference
      REQ-AUTH-001.
- [ ] ASan/LSan clean; zeroize helper proven non-elided (test reads the
      buffer via volatile pointer after the call).
- [ ] CI (Linux gcc/clang + MinGW) builds green with the new dependency.
