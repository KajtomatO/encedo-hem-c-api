---
id: STEP-M12-080
title: "OpenSSL 3.x backend, part 1 — EHEM_CRYPTO_BACKEND=wolfssl|openssl, crypto_shim.c split into crypto_shim_{wolfssl,openssl,common}.c, unit suite green on OpenSSL locally, ./dev build --openssl"
milestone: M12
implements: ["REQ-BUILD-007"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-010", "STEP-M12-060"]
evidence:
  commits: ["4800c19", "08f89db"]   # start; implementation — the done commit follows
  tests:
    - "Pure split first (src/crypto_shim.c → crypto_shim_wolfssl.c by git mv; ehem_zeroize, ehem_serial_hex, the ISO date formatter and the REQ-API-010 test seam → crypto_shim_common.{h,c}): ./dev ci 55/55 gcc + clang on wolfSSL before any OpenSSL code"
    - "OpenSSL backend (dev host OpenSSL 3.0.13): ./dev test --openssl 55/55; ./dev ci --openssl 55/55 gcc + 55/55 clang; ./dev test asan --openssl 55/55 — every fixture-pinned test unchanged (eJWT login vector, BIP39 fixtures, RFC 6979 / 8032 / 4231 / 7748 vectors), test_crypto with no UNSUPPORTED note (compressed P-256 and Ed448 verified), test_cert (d2i_X509 leaf fields), test_ext_sim (AES-128-CBC), test_backend_compat 7/7 (same-major + known answers, both forced refusals through the shared seam, refusal → no-op cleanup → re-init), test_build_info ('OpenSSL', 3.0.13 compiled and loaded), export_symbols, docs_coverage, the consumer gate on the OpenSSL package config; hem-tool --version: 'crypto: OpenSSL 3.0.13 (compiled) / 3.0.13 (loaded), dynamic'"
    - "OPEN criterion: scratch program — OpenSSL-signed messages on P-256, P-384, P-521 and secp256k1 verify through ehem_ecdsa_verify with the compressed (0x02 and 0x03 seen) and the uncompressed point; a tampered message is a clean invalid on each; no decompression needed (EVP_PKEY_fromdata decodes both forms and checks the point is on the curve)"
    - "wolfSSL after the change: ./dev ci 55/55 gcc + clang, ./dev test asan 55/55; GCC 16.2 (conda-forge) over all compile commands of both backends (141 wolfSSL, 138 OpenSSL) at -O0/-O2/-O3: clean; GCC 11.4 + jammy's cmocka 1.1.5 over the Linux release configuration: clean; MinGW cross builds (posix-thread GCC 13) of the wolfSSL configuration against MSYS2 wolfSSL 5.9.4 and of the OpenSSL configuration against MSYS2 OpenSSL 3.6.5: compile and link, no deprecation warning"
    - "crypto_shim.h: git diff across the step = 0 lines; shellcheck dev clean"
  notes: |
    Design: EHEM_CRYPTO_BACKEND (cache string, wolfssl | openssl, anything
    else a FATAL_ERROR naming it). OpenSSL lookup: find_package(OpenSSL 3
    REQUIRED COMPONENTS Crypto) → OpenSSL::Crypto only (OPENSSL_USE_STATIC_LIBS
    under EHEM_DEPS_LINK=static); EHEM_CRYPTO_NAME / _VERSION / _LINK_LIBS
    replace the wolfSSL-only variables (status line, DEPS_LINK enforcement,
    test_build_info's oracle); the static-wolfSSL licensing WARNING and the
    Windows delay-load (now refused on another backend) stay wolfSSL-only.
    The OpenSSL shim: PKCS5_PBKDF2_HMAC, EVP_Q_mac("HMAC","SHA256"),
    EVP_Digest, RAND_bytes, X25519 via EVP_PKEY raw keys + EVP_PKEY_derive
    (the shim clamps the exported scalar; a small-order peer fails the
    derive → PROTOCOL), d2i_X509 + ASN1_STRING data (the INTEGER magnitude,
    no sign byte) + X509_NAME_get_text_by_NID + ASN1_TIME_to_tm, ECDSA via
    EVP_PKEY_fromdata (group name + X9.63 point) + EVP_DigestVerify,
    Ed25519/Ed448 via raw public keys + one-shot EVP_DigestVerify, AES-128-CBC
    via EVP_CipherInit_ex + padding off. A signature that is not exactly one
    canonical DER ECDSA-Sig-Value is a clean invalid (d2i + i2d round-trip
    before the verify) — OpenSSL reports it as an error, the contract (and the
    wolfSSL backend) as a mismatch. Every failure path clears OpenSSL's error
    queue (libcurl in the -static builds uses OpenSSL too). REQ-API-010 on
    OpenSSL: OPENSSL_version_major() == OPENSSL_VERSION_MAJOR, then known
    answers (HMAC-SHA256 RFC 4231 #1, SHA-256("abc")) through the shim; the
    FORCE_VERSION / FORCE_PROBE seam works unchanged (it now lives in
    crypto_shim_common.c). Global cleanup is a no-op. REQ-API-011:
    "OpenSSL", OPENSSL_VERSION_STR vs OpenSSL_version(OPENSSL_VERSION_STRING).
    Pulled forward from STEP-M12-090 because the unit suite needs them on
    OpenSSL: the package-config dependency block and the .pc's
    Requires.private per backend (find_dependency(OpenSSL 3); libcurl
    libcrypto), and the consumer gate's per-backend expectations. Left to
    STEP-M12-090: the three wolfCrypt-using live tests (test_cipher_live,
    test_wrap_live, test_ecdh_live) are registered only on the wolfSSL
    backend until tests/support/crypto_ref replaces their wolfCrypt reference
    computations; the header gate's OpenSSL patterns; the ci.yml openssl legs.
    ./dev: --openssl on build / test <suite> / ci → build[-clang|-asan]-openssl
    (REQ-BUILD-004 rev 3 note). ARCHITECTURE §10 layout names the three
    files. MSYS2 ships OpenSSL 3.6.5 now (the Windows CI leg's version).
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
- [x] `EHEM_CRYPTO_BACKEND` option (`wolfssl` default / `openssl` / error);
      exactly one shim source compiles per configure; `git diff` of
      `src/crypto_shim.h` empty; ARCHITECTURE §10 layout already names
      the three files (edited at the decomposition).
- [x] wolfSSL configure unchanged in outcome after the split: `./dev ci`
      gcc + clang + ASan green, gates green.
- [x] `./dev build --openssl` exists; `./dev ci` (gcc + clang) and `./dev
      test asan` green with `EHEM_CRYPTO_BACKEND=openssl` on the dev host,
      every fixture-pinned test included (eJWT login vector, BIP39
      fixtures, RFC 6979 / 8032 / 4231 vectors).
- [x] REQ-API-002 idempotency test, REQ-API-010 seam test (same-major rule
      + known answer) and REQ-API-011 build info pass on OpenSSL.
- [x] Export gate and docs gate unchanged in outcome on both backends (no
      OpenSSL symbol exported).
- [x] OPEN criterion (compressed-point ECDSA on OpenSSL 3) resolved and
      recorded in REQ-BUILD-007 (rev bump); `implements: REQ-BUILD-007` tag
      in the OpenSSL shim and CMakeLists.txt.
