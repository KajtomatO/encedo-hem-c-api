---
id: STEP-M12-090
title: "OpenSSL 3.x backend, part 2 — tests/support/crypto_ref for the three live tests, header gate extended, consumer gate on OpenSSL, ci.yml openssl legs (Linux + Windows), ./dev test it on OpenSSL"
milestone: M12
implements: ["REQ-BUILD-007"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-070", "STEP-M12-080"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** nothing outside the shim and the test-support layer knows the
backend: the three integration tests that reproduce device crypto with
wolfCrypt directly use a per-backend `tests/support/crypto_ref.{h,c}`;
`public_headers_dep_free` also rejects OpenSSL includes/identifiers; the
REQ-BUILD-008 consumer gate and the whole unit suite + gates run in new
`ci.yml` legs with `EHEM_CRYPTO_BACKEND=openssl` on Linux (distro
`libssl-dev`) and Windows (MSYS2 UCRT64 `openssl`); `./dev test it` is
green on the OpenSSL build against the dev device.

**Notes:** Found at the decomposition (2026-10-12):
`tests/integration/test_cipher_live.c` (`wc_HKDF`, `wc_AesGcm*`),
`test_wrap_live.c` (`wc_AesKeyWrap`, `HAVE_AES_KEYWRAP`),
`test_ecdh_live.c` (`wc_Sha256Hash`) include wolfSSL headers; nothing in
`tests/unit` or `tests/support` does. `crypto_ref.h`: `ref_hkdf_sha256`,
`ref_aes_gcm_encrypt`/`_decrypt`, `ref_aes_keywrap`(`/_unwrap` if used),
`ref_sha256`; `crypto_ref_wolfssl.c` / `crypto_ref_openssl.c` chosen by
the same option (OpenSSL: `EVP_KDF` "HKDF", `EVP_aes_*_gcm`,
`EVP_aes_*_wrap` with `EVP_CIPHER_CTX_FLAG_WRAP_ALLOW`). A CTest grep
gate (or an extension of `check_public_headers.cmake`'s scan) asserts no
file under `tests/` other than `tests/support/crypto_ref*.c` includes a
wolfSSL or OpenSSL header. Header gate: add `openssl/`, `EVP_`, `X509`,
`OSSL_`, `BIO`, `SSL_` to the rejected patterns; negative test with a
planted `#include <openssl/evp.h>` and `EVP_PKEY *` recorded. `ci.yml`:
matrix entries `backend: openssl` for the Linux (gcc + clang) and Windows
jobs — `apt install libssl-dev` / `pacman mingw-w64-ucrt-x86_64-openssl`;
each leg runs the unit suite, the gates and the STEP-M12-070 consumer
gate (that closes REQ-BUILD-008's "on both backends" criterion). Live:
`./dev test it` with the OpenSSL build against the dev device (REQ-TEST-002
gating; the keygen matrix stays `disruptive`) — record pass count and
time; the KNOWN sustained-load stall may need a power-cycle, as at every
gate. The classifier-under-OpenSSL-libcurl criterion of REQ-BUILD-007 is
STEP-M12-120's (needs the static release prefix). MinGW printf gotcha
applies to anything new in the tests.

**Definition of done**
- [ ] `tests/support/crypto_ref.{h,c}` per backend; the three live tests
      compile on both backends with no backend include; the tests-backend-
      free grep gate is a CTest (label `unit`).
- [ ] `public_headers_dep_free` rejects the planted OpenSSL include and
      identifier (negative run recorded) and passes the real headers on
      both backends.
- [ ] `ci.yml` openssl legs (Linux gcc + clang, Windows MSYS2) green on
      push: unit suite, gates, consumer gate (REQ-BUILD-008 OpenSSL
      criteria checked); run id recorded.
- [ ] `./dev test it` green on the OpenSSL build against the dev device
      (count + time recorded in REQ-BUILD-007).
- [ ] tests/README documents the backend switch and the new gate;
      `supports:`/`verifies:` tags on crypto_ref and the gate.
- [ ] REQ-BUILD-007 criteria all checked except the release-classifier
      one (STEP-M12-120); `./dev ci` green on both backends locally.
