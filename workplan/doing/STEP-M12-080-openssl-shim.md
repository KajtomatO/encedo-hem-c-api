---
id: STEP-M12-080
title: "OpenSSL 3.x backend, part 1 — EHEM_CRYPTO_BACKEND=wolfssl|openssl, crypto_shim.c split into crypto_shim_{wolfssl,openssl,common}.c, unit suite green on OpenSSL locally, ./dev build --openssl"
milestone: M12
implements: ["REQ-BUILD-007"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-010", "STEP-M12-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `EHEM_CRYPTO_BACKEND` (`wolfssl` default, `openssl`, anything
else a configure error) selects exactly one shim source;
`src/crypto_shim.c` becomes `crypto_shim_wolfssl.c` + `crypto_shim_openssl.c`
+ `crypto_shim_common.c` behind an unchanged `src/crypto_shim.h`; the full
unit suite — every byte-exact fixture — and ASan pass on the OpenSSL
build on the dev host (OpenSSL 3.0.13 is fine for development); the
REQ-API-010 check on OpenSSL is same-major + known-answer and REQ-API-011
reports `OpenSSL`; `./dev build --openssl`.

**Notes:** Do the split first as a pure move (wolfSSL still green —
includes at crypto_shim.c:17-31 go with the wolfSSL file; `ehem_zeroize`,
`ehem_serial_hex`, the ISO date formatter to common), then write the
OpenSSL file against the header's contract: PBKDF2-HMAC-SHA256/-SHA512 →
`PKCS5_PBKDF2_HMAC` (`EVP_sha256()`/`EVP_sha512()`); HMAC-SHA256 →
`EVP_MAC_fetch("HMAC")`; SHA-256 → `EVP_Digest`; X25519 keypair-from-seed
+ shared secret → `EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519)` /
`EVP_PKEY_derive` (clamp the exported scalar as today); random →
`RAND_bytes`; X.509 leaf parse → `d2i_X509` + serial via
`ASN1_INTEGER_to_BN` → `ehem_serial_hex`, subject CN
`X509_NAME_get_text_by_NID(NID_commonName)`, notBefore/notAfter via
`ASN1_TIME_to_tm` → the common ISO formatter; ECDSA verify (DER sig,
paired digest, compressed or uncompressed X9.63 point) and Ed25519/Ed448
verify → `EVP_DigestVerify` (Ed* with `EVP_DigestVerifyInit` + one-shot);
AES-128-CBC no padding (REQ-TEST-006 simulator) → `EVP_EncryptInit_ex` +
`EVP_CIPHER_CTX_set_padding(0)`. Tri-state verify results and every
return code exactly as the header says. OPEN criterion to resolve and
record (rev bump): compressed-point ECDSA keys — `EC_POINT_oct2point`
accepts compressed encodings in the default provider (expected); if a
curve refuses, decompress in the shim and say which. Global init:
`OPENSSL_init_crypto(0, NULL)`; cleanup is a **no-op** (`OPENSSL_cleanup`
is one-shot and would break REQ-API-002's init-after-cleanup). REQ-API-010
on this backend: `OPENSSL_version_major()` == `OPENSSL_VERSION_MAJOR` +
HMAC/SHA known answers through the shim; the STEP-M12-010 seam works
here too. REQ-API-011: `"OpenSSL"`, `OPENSSL_VERSION_STR` vs
`OpenSSL_version(OPENSSL_VERSION)`. CMake: `find_package(OpenSSL 3
REQUIRED)` → link `OpenSSL::Crypto` only (libssl is libcurl's business);
`--exclude-libs,ALL` keeps OpenSSL out of the export (export gate), hidden
visibility as today; the header gate's OpenSSL patterns are STEP-M12-090's.
`./dev build --openssl` (REQ-BUILD-004). The cross MinGW toolchain on the
dev host has no OpenSSL headers — the Windows OpenSSL proof is the MSYS2
CI leg of STEP-M12-090; keep the MinGW syntax check for the wolfSSL
configuration. Dev host facts: wolfSSL 5.6.6, OpenSSL 3.0.13; release pin
(3.5 LTS line) is STEP-M12-110/120's.

**Definition of done**
- [ ] `EHEM_CRYPTO_BACKEND` option (`wolfssl` default / `openssl` / error);
      exactly one shim source compiles per configure; `git diff` of
      `src/crypto_shim.h` empty; ARCHITECTURE §10 layout already names
      the three files (edited at the decomposition).
- [ ] wolfSSL configure unchanged in outcome after the split: `./dev ci`
      gcc + clang + ASan green, gates green.
- [ ] `./dev build --openssl` exists; `./dev ci` (gcc + clang) and `./dev
      test asan` green with `EHEM_CRYPTO_BACKEND=openssl` on the dev host,
      every fixture-pinned test included (eJWT login vector, BIP39
      fixtures, RFC 6979 / 8032 / 4231 vectors).
- [ ] REQ-API-002 idempotency test, REQ-API-010 seam test (same-major rule
      + known answer) and REQ-API-011 build info pass on OpenSSL.
- [ ] Export gate and docs gate unchanged in outcome on both backends (no
      OpenSSL symbol exported).
- [ ] OPEN criterion (compressed-point ECDSA on OpenSSL 3) resolved and
      recorded in REQ-BUILD-007 (rev bump); `implements: REQ-BUILD-007` tag
      in the OpenSSL shim and CMakeLists.txt.
