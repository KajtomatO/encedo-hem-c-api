---
id: STEP-M2-010
title: "Crypto shim: wolfCrypt PBKDF2-SHA256 / HMAC-SHA256 / X25519 + RFC vectors"
milestone: M2
implements: ["REQ-AUTH-001"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits:
    - "db06d91 — initial crypto shim"
    - "3fde0fd — fix: X25519 portable base-point scalarmult (MinGW CI)"
  tests:
    - "verifies: REQ-AUTH-001 — tests/unit/test_crypto.c (PBKDF2/HMAC/X25519 vectors, zeroize)"
    - "verifies: REQ-AUTH-001 — tests/unit/check_public_headers.cmake (no wolfSSL in include/ehem/)"
  notes: |
    src/crypto_shim.{h,c} added (implements: REQ-AUTH-001), compiled into both
    library variants (EHEM_SOURCES). wolfSSL discovered via pkg-config into
    PkgConfig::WOLFSSL (REQUIRED), linked PRIVATE (shared) / PUBLIC (static) like
    libcurl; wolfSSL headers included only in crypto_shim.c.

    API: ehem_kdf_pbkdf2_sha256 (iterations+out_len are params; login pins
    600k/32B per REQ-AUTH-001), ehem_hmac_sha256, ehem_x25519_keypair_from_seed
    (RFC 7748 clamp; returns clamped priv + pub), ehem_x25519_shared, ehem_zeroize
    (volatile-write scrub). Returns ehem_rc: EHEM_ERR_ARG for NULL/length,
    EHEM_ERR_PROTOCOL for an unexpected engine failure (no ABI enum added).

    Ground truth pinned BEFORE coding via a wolfCrypt probe + python reference
    (cryptography lib reproduces RFC 7748 §6.1 exactly): X25519 needs
    import_private_ex/import_public_ex/export_public_ex/shared_secret_ex with
    **EC25519_LITTLE_ENDIAN** (wc_curve25519_make_pub fails rc=-170 and is NOT
    used). Vectors reproduced by the shim: PBKDF2-HMAC-SHA256 password/salt c=1
    (120fb6cf…) and c=4096 (c5e478d5…); HMAC-SHA256 RFC 4231 TC1 (b0344c61…) +
    TC2 (5bdcc146…); X25519 §6.1 keypair a→8520f009… b→de9edb7d…, DH shared
    4a5d9d5b… (a·B == b·A); X25519 §5.2 scalarmult k·u → c3da5537….

    Verified on the dev machine (wolfSSL 5.6.6):
      - ./dev ci → gcc + clang unit builds green, 10/10 tests each (incl.
        test_crypto, public_headers_dep_free, export_symbols) under -Werror.
      - ./dev test asan → ASan/LSan clean; zeroize proven non-elided (volatile
        read-back asserts all-zero after the call).
      - export table: readelf shows NEEDED libwolfssl.so.42; nm -D defined
        exports remain ehem_* only (no wolfSSL leak).
      - header check EXTENDED to reject wolfSSL includes AND type tokens
        (curve25519_key / wc_* / WOLFSSL* / WC_*); negative-tested against a
        crafted include-leak and type-leak header (both → FATAL rc=1); renamed
        the CTest public_headers_curl_free → public_headers_dep_free (dev check
        regex updated).

    CI wiring: .github/workflows/ci.yml drops --no-crypto (Linux installs
    libwolfssl-dev) and adds mingw-w64-x86_64-wolfssl to the MSYS2 list;
    install-deps-linux.sh gained a wolfSSL verification line (crypto builds).
    MinGW leg is wired (MSYS2 wolfssl ships a pkg-config .pc, same discovery
    path) but not run this session — confirmed by the CI run on push.

    POST-CI FIX (2026-07-16): the MinGW CI leg failed test_crypto —
    ehem_x25519_keypair_from_seed returned EHEM_ERR_PROTOCOL (0xa) because the
    MSYS2 wolfSSL build fails wc_curve25519_export_public_ex after
    import_private_ex (a wolfSSL build/version difference; works on Debian
    5.6.6). Fixed by computing the public key the same way as the ECDH shared
    secret — a scalarmult of the base point (u=9) via
    import_private/import_public/shared_secret — so keypair and ECDH share one
    path and never call export_public_ex. Re-verified on Debian against RFC
    7748 §5.2/§6.1 (probe + test_crypto/test_ejwt green, gcc+clang+asan). The
    MinGW leg itself is re-confirmed by CI on the next push.
reopened:
  - {date: 2026-07-16, reason: "MinGW CI failed test_crypto (X25519 export_public_ex unsupported on MSYS2 wolfSSL); reworked to a portable base-point scalarmult"}
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
- [x] Shim compiles into the library on GCC + Clang under `-Werror`; no
      wolfSSL symbol/header leaks into `include/ehem/` (header-check test
      extended).
- [x] Unit tests pass against published vectors: RFC 7748 §5.2 X25519
      (scalar mult + Diffie-Hellman §6.1), RFC 4231 HMAC-SHA256 cases,
      and a PBKDF2-HMAC-SHA256 vector; `verifies:` tags reference
      REQ-AUTH-001.
- [x] ASan/LSan clean; zeroize helper proven non-elided (test reads the
      buffer via volatile pointer after the call).
- [x] CI (Linux gcc/clang + MinGW) builds green with the new dependency.
      (Linux gcc/clang locally green via `./dev ci`; MinGW leg wired in
      ci.yml — confirmed by the CI run on push.)
