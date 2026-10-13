---
id: REQ-API-010
title: Crypto-backend compatibility check at ehem_global_init — a foreign library is refused with a distinct error, never masked
status: approved
priority: must
revision: 2   # 2026-10-12 (STEP-M12-010): OPEN criterion resolved (DecodedCert probe parses an embedded 375-byte self-signed EC leaf; WC_RNG probed init + one 16-byte draw + free, canaries only, an init failure is NOMEM-class not a mismatch); unit-proven criteria ticked. No meaning change — approved status kept
source: user decision 2026-10-10 (the v1.1.0-rc4 crash on a foreign libwolfssl is fixed in M12 by an SDK version check + self-test, not in M10); ARCHITECTURE.md §11 M12 part A (planned 2026-10-12), §12 risk 5 (caller-allocated wolfSSL structs are an ABI hazard); KNOWN-ISSUES.md "Release hem-tool with a foreign libwolfssl crashes" (Hmac 784 vs 896 bytes, DecodedCert 1528 vs 2576 — 5.7.2 release build vs Ubuntu 5.6.6, same SONAME); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-API-002", "REQ-API-003", "REQ-AUTH-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Crypto-backend compatibility check at ehem_global_init

The SDK SHALL refuse to operate on a crypto library other than the one it
was built against: `ehem_global_init()` checks the library loaded at run
time before any other use of it and fails with the new, append-only
`ehem_rc` value **`EHEM_ERR_BACKEND_MISMATCH`** (after
`EHEM_ERR_UNSUPPORTED`), and every path that initialises implicitly —
`ehem_ctx_create()` standing up the default transport, the login flow's
self-initialisation — returns that code to the caller instead of the
`EHEM_ERR_NOMEM` it reports today.

- **wolfSSL backend (the default):** compatible = the exact version
  (`wolfSSL_lib_version_hex()` == compile-time `LIBWOLFSSL_VERSION_HEX`)
  **and** a known-answer self-test of every wolfCrypt struct the SDK
  allocates itself — `Hmac`, `WC_RNG`, `DecodedCert`, `Aes`,
  `ed25519_key`, `ed448_key` (`ecc_key` is library-allocated via
  `wc_ecc_key_new` and needs no probe). Each probe runs the struct's init
  / one operation / free inside a buffer with canary bands on both sides
  and compares a known answer; a changed canary or wrong answer is a
  mismatch. The self-test catches what the version check cannot: a
  same-version library built with other options (`options.h` decides the
  struct layouts — KNOWN-ISSUES "Residual notes").
- **OpenSSL backend (REQ-BUILD-007):** compatible = the same major
  version (`OPENSSL_version_major()` == compile-time
  `OPENSSL_VERSION_MAJOR`) plus a known-answer check of HMAC-SHA256 and
  SHA-256 through the shim. OpenSSL 3.x objects are opaque and the 3.x
  ABI is stable across minors, so the layout hazard does not exist; an
  exact-version rule would only break distro upgrades.
- The check runs once per successful `ehem_global_init` (the pair stays
  idempotent, REQ-API-002) and costs microseconds. A refused init leaves
  the process-global state untouched: `ehem_global_cleanup()` is a no-op
  afterwards and a later `ehem_global_init()` re-checks.
- There is no context at global init, so the refusal carries no
  `ehem_last_error` detail; the versions involved are retrievable through
  REQ-API-011 so a caller (hem-tool, REQ-TOOL-024) can compose the
  message.

**Rationale:** the 1.1.0 release hem-tool, run without its companion
`libwolfssl.so.42`, loaded the host's same-SONAME wolfSSL 5.6.6 and
aborted with "stack smashing detected" — a struct the SDK had sized at
compile time was overrun by a library that sizes it differently
(KNOWN-ISSUES). Caller-allocated structs are the price of wolfCrypt's
API; the SDK can make the hazard fail safe. A distinct error code is
required because the consumer (encedo-pkcs11) must tell "wrong library
installed" from "out of memory" (HEM-SDK-8 distinguishable conditions).

**Acceptance criteria:**
- [x] `EHEM_ERR_BACKEND_MISMATCH` is appended to `ehem_rc` after
      `EHEM_ERR_UNSUPPORTED` and never renumbered; `ehem_rc_str()` maps
      it (the enum-iterating unit test of REQ-API-003 covers it);
      docs/API-GUIDE.md "Error model" lists it (docs gate).
      *(2026-10-12, STEP-M12-010: `include/ehem/ehem.h`, `src/context.c`;
      `test_context.c` iterates to the new last value;
      `test_backend_compat.c` asserts value = UNSUPPORTED + 1 and the name.)*
- [x] wolfSSL: the version check and the six guard-banded probes exist in
      the shim; on the matching library `ehem_global_init()` succeeds
      (the whole unit suite is that proof, both CI platforms).
      *(`src/crypto_shim.c` "Backend compatibility check": exact
      `wolfSSL_lib_version_hex()` == `LIBWOLFSSL_VERSION_HEX`, then probes
      Hmac (RFC 4231 #1), WC_RNG, DecodedCert (embedded leaf), Aes (FIPS-197
      C.1), ed25519_key, ed448_key (OpenSSL-produced signatures) in heap boxes
      with a 64-byte leading and a 4096-byte trailing canary band. Linux
      proof 2026-10-12: `./dev ci` 48/48 gcc + clang, ASan 48/48 against the
      dev host's wolfSSL 5.6.6; the Windows leg is CI's.)*
- [x] A hidden (non-exported) test seam forces (a) a version mismatch and
      (b) a probe failure; each yields `EHEM_ERR_BACKEND_MISMATCH` from
      `ehem_global_init()`, with no wolfCrypt call other than the probes
      having run (unit test).
      *(Seam `ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION |
      _PROBE)` in `crypto_shim.h` — named after the existing
      `ehem_auth_test_set_*` seams; the version refusal returns before
      `wolfCrypt_Init`, the probe refusal corrupts the Hmac known answer so
      the comparison path refuses. `tests/unit/test_backend_compat.c`.)*
- [x] Implicit init: with the seam forcing a mismatch, `ehem_ctx_create()`
      returns `EHEM_ERR_BACKEND_MISMATCH` (not `EHEM_ERR_NOMEM`) and so
      does a login over a caller-supplied transport (unit tests).
      *(`ehem_ctx_create` runs `ehem_global_init()` before standing up the
      default transport and returns its verdict; the login flow's
      self-initialisation is at the first token acquisition
      (`ehem_login()` only stores the credential) — `proto_auth.c` already
      carried the real code; the test drives `ehem_auth_ensure_token` over
      the fake transport: `EHEM_ERR_BACKEND_MISMATCH`, zero requests.)*
- [x] After a refused init, `ehem_global_cleanup()` is a no-op and a
      following `ehem_global_init()` (seam cleared) succeeds (unit test —
      the REQ-API-002 idempotency criterion extended).
      *(A probe refusal undoes its own `wolfCrypt_Init`; `g_global_ready`
      is never set on a refusal. `test_refusal_leaves_global_state_untouched`.)*
- [ ] OpenSSL: the same-major rule and the known-answer check are in the
      OpenSSL shim; the seam test runs on that backend too (CI leg of
      REQ-BUILD-007).
- [ ] Demonstrated at the M12 gate: the release Linux hem-tool run with
      the host's `libwolfssl.so.42` 5.6.6 and no companion library refuses
      with the REQ-TOOL-024 message and exit code instead of crashing —
      recorded here with date, binary and versions.
- [x] RESOLVED at STEP-M12-010 (2026-10-12): the `DecodedCert` probe parses
      a tiny self-signed EC leaf embedded in the shim (375 bytes DER,
      CN `ehem-probe`, serial `0102030405`, produced with OpenSSL 3.0.13 —
      recipe in the source) and compares the four fields
      `ehem_cert_parse_leaf()` reads (serial, CN, notBefore, notAfter), so
      a field-offset difference is caught, not only a size difference.
      `WC_RNG` IS probed with `wc_InitRng` + one 16-byte draw + `wc_FreeRng`,
      canaries only (a DRBG has no known answer); a failed `wc_InitRng` on an
      intact box is reported as `EHEM_ERR_NOMEM`-class (the shim's existing
      verdict for "no entropy"), never as a mismatch. Likewise a failed box
      allocation is `EHEM_ERR_NOMEM`, and `ed448_key` on a build without
      `HAVE_ED448` (`NOT_COMPILED_IN`) passes — there is no struct to probe
      and `ehem_ed448_verify` reports UNSUPPORTED on such a build anyway.
