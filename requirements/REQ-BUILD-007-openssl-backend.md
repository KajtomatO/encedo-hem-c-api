---
id: REQ-BUILD-007
title: OpenSSL 3.x crypto backend behind the unchanged crypto shim — EHEM_CRYPTO_BACKEND=wolfssl|openssl
status: approved
priority: must
revision: 1
source: user decision 2026-10-12 (M12 planning: the alternative library is OpenSSL 3.x — Apache-2.0, so it may be linked statically; static packages use OpenSSL, never wolfSSL); ARCHITECTURE.md §1 (crypto backend switch, wolfSSL the default), §11 M12 part D, §12 risk 1; REQ-AUTH-001 (the shim's contract), REQ-AUTH-012 (PBKDF2-SHA512, SHA-256), REQ-SYS-006 (X.509 leaf parse), REQ-OPS-001 (local ECDSA/EdDSA verify), REQ-TEST-006 (AES-128-CBC); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-AUTH-001", "REQ-API-002", "REQ-API-010", "REQ-BUILD-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# OpenSSL 3.x crypto backend behind the unchanged crypto shim

The SDK SHALL build and pass its unit suite with OpenSSL 3.x as the
crypto backend, selected at configure time by
`EHEM_CRYPTO_BACKEND=openssl` (default `wolfssl`), behind the unchanged
internal `src/crypto_shim.h` — no public header, no caller and no test
outside the shim and the test-support layer knows which backend is in
use.

- **Sources:** `src/crypto_shim.c` splits into `src/crypto_shim_wolfssl.c`
  and `src/crypto_shim_openssl.c` (one compiled per configure) plus the
  backend-free helpers (`ehem_zeroize`, `ehem_serial_hex`, the ISO date
  formatter) in `src/crypto_shim_common.c`. The shim header's contract —
  every function, every return code, including the tri-state verify
  results — is unchanged.
- **Primitive mapping (OpenSSL 3, default provider only):** PBKDF2-HMAC-
  SHA256/-SHA512 → `PKCS5_PBKDF2_HMAC`; HMAC-SHA256 → `EVP_MAC`
  ("HMAC"); SHA-256 → `EVP_Digest`; X25519 keypair-from-seed and shared
  secret → `EVP_PKEY_new_raw_private_key` / `EVP_PKEY_derive` (RFC 7748
  clamping of the exported private scalar done by the shim as today);
  random bytes → `RAND_bytes`; X.509 leaf parse → `d2i_X509` + serial /
  subject CN / notBefore / notAfter; ECDSA verify (DER signature, the
  curve's paired digest, compressed or uncompressed X9.63 point) and
  Ed25519 / Ed448 verify → `EVP_DigestVerify`; AES-128-CBC without
  padding → `EVP_EncryptInit_ex` + `EVP_CIPHER_CTX_set_padding(0)`.
  Nothing the SDK needs is missing from OpenSSL 3, so the
  `EHEM_ERR_UNSUPPORTED` paths that guard exotic wolfSSL builds
  (no `HAVE_ED448`, no `HAVE_COMP_KEY`) never trigger on this backend.
- **Global init / cleanup:** OpenSSL 3 self-initialises; the shim calls
  `OPENSSL_init_crypto` for determinism and its cleanup is a **no-op**
  (`OPENSSL_cleanup` is one-shot and would break REQ-API-002's
  init-after-cleanup idempotency). The REQ-API-010 compatibility check
  on this backend is the same-major rule plus the known-answer check.
- **Containment gate:** `public_headers_dep_free` (tests/unit/
  check_public_headers.cmake) also rejects OpenSSL includes and
  identifiers (`openssl/`, `EVP_`, `X509`, `OSSL_`, `BIO`, `SSL_`).
- **Tests:** the unit suite and its byte-exact fixtures (the eJWT login
  vector, the BIP39 fixtures from the Manager's JavaScript, the RFC
  6979 / RFC 8032 / RFC 4231 vectors) pass unchanged on both backends —
  the proof the primitives agree. The three integration tests that
  reproduce device crypto with wolfCrypt directly (`test_cipher_live.c`:
  HKDF-SHA256 + AES-GCM; `test_wrap_live.c`: AES key wrap;
  `test_ecdh_live.c`: SHA-256) move those reference computations into a
  per-backend test-support module (`tests/support/crypto_ref.{h,c}`),
  so `./dev test it` runs on both backends and no test includes a
  backend header directly.
- **Transport:** the expired-certificate classifier (REQ-NET-005)
  already recognises OpenSSL's verdict (verify result 10, "certificate
  has expired") — the original path; it is proven again under the
  OpenSSL-backed libcurl of the static release builds (REQ-BUILD-009,
  `check-expired-classifier.sh`).
- **Tooling:** `./dev build --openssl` (REQ-BUILD-004); `ci.yml` gains a
  Linux and a Windows leg with `EHEM_CRYPTO_BACKEND=openssl` against the
  distro / MSYS2 OpenSSL 3 (unit suite + gates on every push).
- **Version line:** every OpenSSL 3.x works for development; the release
  pin (REQ-BUILD-009) is the **3.5 LTS line** — 3.0 LTS reached end of
  life on 2026-09-07 (openssl.org release policy), so a 3.0 pin would
  ship an unsupported library.

**Rationale:** no published package may bundle wolfSSL statically (§1,
user decision 2026-10-09), yet operators want one-file binaries and
consumers a static SDK. OpenSSL 3.x is Apache-2.0, covers every
primitive the shim needs (Ed448 included), is libcurl's main TLS backend,
and its opaque objects make the struct-size hazard of REQ-API-010
impossible by construction. The shim was built as the single wolfCrypt
includer for exactly this reason (ARCHITECTURE §3 containment rule).

**Acceptance criteria:**
- [ ] `EHEM_CRYPTO_BACKEND` exists (`wolfssl` default, `openssl`, anything
      else a configure error); exactly one shim source compiles per
      configure; `src/crypto_shim.h` has no diff.
- [ ] `./dev ci` (gcc + clang) and `./dev test asan` are green with
      `EHEM_CRYPTO_BACKEND=openssl` on Linux — every fixture-pinned test
      included; the MSYS2 OpenSSL build passes `ctest -L unit` on
      Windows (CI legs on both platforms).
- [ ] `public_headers_dep_free` rejects a planted `#include <openssl/evp.h>`
      and a planted `EVP_PKEY *` in a public header (negative test
      recorded), passes the real headers on both backends.
- [ ] Integration suite (`./dev test it`) green on the OpenSSL build
      against the dev device, including the three reference-computation
      tests through `crypto_ref`; no file under `tests/` other than
      `tests/support/crypto_ref*.c` includes a wolfSSL or OpenSSL header
      (grep).
- [ ] REQ-API-002 idempotency test (double init / double cleanup /
      init-after-cleanup) passes on the OpenSSL backend; REQ-API-010's
      same-major + known-answer check and REQ-API-011's build info are
      exercised there.
- [ ] Export gate and docs gate unchanged in outcome on both backends
      (no OpenSSL symbol exported from the shared library — with static
      OpenSSL this needs the existing `--exclude-libs,ALL`).
- [ ] OPEN (needs verification at the step): compressed-point ECDSA
      public keys via `EVP_DigestVerify` on OpenSSL 3 — expected to work
      through `EC_POINT_oct2point`; if the default provider rejects the
      compressed form for some curve, the shim decompresses (record which).
