---
id: STEP-M12-090
title: "OpenSSL 3.x backend, part 2 — tests/support/crypto_ref for the three live tests, header gate extended, consumer gate on OpenSSL, ci.yml openssl legs (Linux + Windows), ./dev test it on OpenSSL"
milestone: M12
implements: ["REQ-BUILD-007"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-070", "STEP-M12-080"]
evidence:
  commits: ["46e3882", "da9d14a", "91b447f"]   # first start (parked, see notes); restart; the implementation (cherry-picked from e84a29f) — the CI run and the done commit follow
  tests:
    - "tests/support/crypto_ref.h + crypto_ref_wolfssl.c / crypto_ref_openssl.c (supports: REQ-BUILD-007): ref_sha256, ref_hkdf_sha256 (wc_HKDF / EVP_KDF \"HKDF\"), ref_aes_gcm_encrypt (wc_AesGcm* / EVP_aes_*_gcm), ref_aes_keywrap (wc_AesKeyWrap under HAVE_AES_KEYWRAP, else CRYPTO_REF_UNSUPPORTED / EVP_aes_*_wrap with EVP_CIPHER_CTX_FLAG_WRAP_ALLOW); compiled into ehem_test_support per backend. test_cipher_live, test_wrap_live, test_ecdh_live use it — no backend include left under tests/ outside crypto_ref_*.c; the three now build on both backends"
    - "tests_backend_free (tests/unit/check_tests_backend_free.cmake, label unit, verifies: REQ-BUILD-007): passes on the tree; a copy with `#include <openssl/evp.h>` planted in unit/test_crypto.c → FAIL; it also fails if either crypto_ref file stops including its library (the scan must find both)"
    - "public_headers_dep_free extended (openssl/ includes; EVP_, X509, OSSL_, BIO, SSL_ identifiers): planted include → FAIL, planted EVP_PKEY * → FAIL, real headers → pass"
    - "Local: ./dev ci 56/56 gcc + clang; ./dev ci --openssl 56/56 gcc + clang; ./dev test asan 56/56 and --openssl 56/56; MinGW cross builds of both backends (MSYS2 wolfSSL 5.9.4 without key wrap / OpenSSL 3.6.5) compile and link incl. crypto_ref and the three live tests; GCC 16.2 over both backends' 142 compile commands -O2: clean"
    - "Live, 2026-10-13: ./dev test it --openssl against the dev device (my.ence.do, fw v1.2.2-DIAG): 23/23 green in 312 s — incl. test_wrap_live '[probe] ECDH-KEK wrap vs local OpenSSL key wrap (HKDF info encedo-kek‖ctx): BYTE-EXACT MATCH', test_cipher_live's derived-mode HKDF/AES-GCM reproduction and test_ecdh_live's reference SHA-256 through crypto_ref_openssl.c"
    - "ci.yml: Linux gcc + clang OpenSSL legs (libssl-dev step), Windows OpenSSL leg (openssl:p); the link assertion for the OpenSSL legs (hem-tool NEEDS / imports libcrypto, never libwolfssl) simulated locally: PASS on the OpenSSL build, FAIL on the wolfSSL build"
    - "After the restart, 2026-10-14 (91b447f — src/, the unit/integration/support tests, CMake and the workflows byte-identical to the parked e84a29f; the difference is STEP-M12-070's consumer-gate fixes): ./dev ci 56/56 gcc + clang; ./dev ci --openssl 56/56 gcc + clang; ./dev test asan 56/56 and --openssl 56/56. The OpenSSL install the gate checks: encedo-hem-config.cmake → find_dependency(CURL) + find_dependency(OpenSSL 3); encedo-hem.pc → Requires.private: libcurl libcrypto, the rest as on wolfSSL"
  notes: |
    PROCESS: this step was started (46e3882) while its dependency
    STEP-M12-070 was still in doing/ (waiting for its CI legs) — a breach of
    the §5.4 precondition, mine (I read only 080 as its dependency). The work
    was complete and parked on the local branch park/step-m12-090; the step
    went back to todo/ on development (a023e4b). Restarted 2026-10-14 once
    070 was done (f2f4de9): git mv todo→doing (da9d14a), the parked
    commit cherry-picked — only this file conflicted.
    PENDING for done (after the restart): the ci.yml openssl legs green
    (Linux gcc + clang, Windows MSYS2 — unit suite, gates, consumer gate on
    the OpenSSL package config, which closes REQ-BUILD-008's "on both
    backends" criterion).
    Design: crypto_ref is the test-side twin of the crypto shim — the device's
    computations reproduced with the build's library directly, independent of
    the shim, one file per backend, the only backend includes under tests/.
    test_wrap_live keeps its MSYS2 path as a run-time CRYPTO_REF_UNSUPPORTED
    (was a compile-time #ifdef). test_ecdh_live could have used the shim's
    ehem_sha256 but takes ref_sha256 to stay an independent check.
    test_sign_live's comments now say "the crypto shim", not wolfCrypt.
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
- [x] `tests/support/crypto_ref.{h,c}` per backend; the three live tests
      compile on both backends with no backend include; the tests-backend-
      free grep gate is a CTest (label `unit`).
      (crypto_ref_wolfssl.c / crypto_ref_openssl.c are the only test sources
      with a crypto library include; `tests_backend_free`, label `unit`.)
- [x] `public_headers_dep_free` rejects the planted OpenSSL include and
      identifier (negative run recorded) and passes the real headers on
      both backends.
      (Negative runs in the evidence; passes in `./dev ci` and
      `./dev ci --openssl`.)
- [ ] `ci.yml` openssl legs (Linux gcc + clang, Windows MSYS2) green on
      push: unit suite, gates, consumer gate (REQ-BUILD-008 OpenSSL
      criteria checked); run id recorded.
- [x] `./dev test it` green on the OpenSSL build against the dev device
      (count + time recorded in REQ-BUILD-007).
      (23/23 in 312 s, 2026-10-13, on e84a29f; the restarted tree's src/
      and tests are byte-identical to it.)
- [x] tests/README documents the backend switch and the new gate;
      `supports:`/`verifies:` tags on crypto_ref and the gate.
      ("Two crypto backends"; `supports:` on crypto_ref.h and both
      implementations, `verifies:` on check_tests_backend_free.cmake and
      check_public_headers.cmake.)
- [ ] REQ-BUILD-007 criteria all checked except the release-classifier
      one (STEP-M12-120); `./dev ci` green on both backends locally.
