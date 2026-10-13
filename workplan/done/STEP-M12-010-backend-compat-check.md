---
id: STEP-M12-010
title: "SDK crypto-backend compatibility check at ehem_global_init — EHEM_ERR_BACKEND_MISMATCH, wolfSSL version check + guard-banded self-test, no NOMEM masking, hidden test seam"
milestone: M12
implements: ["REQ-API-010"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-API-010 — tests/unit/test_backend_compat.c (7: the matching library passes version + six probes; forced version mismatch and forced probe failure each → EHEM_ERR_BACKEND_MISMATCH, re-derived on every call; refusal leaves cleanup a no-op and a cleared re-init succeeds (REQ-API-002 extended); ehem_ctx_create with the default transport → BACKEND_MISMATCH and *out NULL, both forcings; login flow over the fake transport → BACKEND_MISMATCH at the first acquisition with zero requests and the 'process-global init failed' message; value == UNSUPPORTED + 1 and ehem_rc_str names it (REQ-API-003))"
    - "tests/unit/test_context.c test_rc_str_covers_every_value iterates to EHEM_ERR_BACKEND_MISMATCH (sentinel moved to +1)"
    - "2026-10-12: ./dev ci 48/48 gcc + clang (export_symbols, public_headers_dep_free, docs_coverage, ascii_strings green); ./dev test asan 48/48; x86_64-w64-mingw32-gcc -fsyntax-only clean for src/context.c and src/crypto_shim.c (Linux wolfSSL headers via -I; the real Windows build is CI's windows-mingw leg)"
  notes: >
    2026-10-12. Shim (src/crypto_shim.c, section "Backend compatibility
    check", implements: REQ-API-010 at line 502): step 1 exact version —
    wolfSSL_lib_version_hex() (wolfssl/ssl.h) vs LIBWOLFSSL_VERSION_HEX
    (wolfssl/version.h), a plain accessor safe to ask ANY library, before
    wolfCrypt_Init so a refused library has had nothing of ours touch its
    global state; step 2 wolfCrypt_Init; step 3 six probes in heap boxes
    (probe_open/probe_close: 64-byte leading + 4096-byte trailing canary
    0xA5 bands around OUR sizeof — heap, not stack, so an oversized foreign
    struct lands in our bytes; the 5.6.6/5.7.2 DecodedCert delta was 1048).
    Probes: Hmac = RFC 4231 case 1; WC_RNG = init + 16-byte draw + free,
    canaries only; DecodedCert = embedded 375-byte self-signed EC leaf
    (CN ehem-probe, serial 0102030405, OpenSSL 3.0.13 recipe in the source)
    compared on serial/CN/notBefore/notAfter via the same helpers
    ehem_cert_parse_leaf uses — catches field-offset drift, not only size;
    Aes = FIPS-197 C.1 (CBC zero IV == ECB); ed25519_key / ed448_key = verify
    OpenSSL-produced signatures over "encedo-hem crypto backend probe"
    (second implementation → independent answers). Verdicts: PASS /
    MISMATCH / NOMEM — NOMEM (box malloc failure, or wc_InitRng failing on
    an intact box = no entropy) is EHEM_ERR_NOMEM, never a mismatch;
    ed448 NOT_COMPILED_IN passes (no struct to probe; ehem_ed448_verify
    reports UNSUPPORTED on such a build). A probe refusal undoes its own
    wolfCrypt_Init. Seam: ehem_crypto_test_force_mismatch(mode) in
    crypto_shim.h — named after the existing ehem_auth_test_set_* seams
    rather than the plan's ehem__crypto_force_mismatch; FORCE_VERSION
    XORs the loaded version (refusal before any wolfCrypt call),
    FORCE_PROBE flips a byte of the Hmac answer (the comparison path
    refuses). Hidden visibility, absent from the export baseline (gate
    green). context.c: ehem_ctx_create() calls ehem_global_init() before
    ehem_transport_default_new() and returns its code (the factory's own
    init call stays, idempotent) — the 1.1.0 NOMEM masking is gone; the
    login flow (proto_auth.c:368 acquire_token) already carried the real
    rc, no change — note ehem_login() only stores the credential, the
    self-init happens at the first acquisition, which is what the test
    drives (ehem_auth_ensure_token). ehem.h: enum value appended with a
    comment, global-init block documents the check. API-GUIDE: error
    model paragraph, global-state paragraph, symbol table row.
    KNOWN-ISSUES "foreign libwolfssl" entry: Status → fixed on development
    (hem-tool message = STEP-M12-020, demo = STEP-M12-140, 1.1.0 binaries
    still crash); heading stays OPEN until the gate demo. REQ-API-010 rev 2
    (OPEN resolved, five criteria ticked, approved status kept) → verified
    at the §4.3 regeneration. No new exported symbol; MinGW gotcha
    irrelevant here (no ehem_ctx_fail in the shim). The dev host's system
    wolfSSL 5.6.6 is the development build's MATCHING library (headers and
    .so agree), so the foreign-library demo remains the gate's; the
    release-built binary vs the host library is the real case.
reopened: []
cancelled: null
---

**Goal:** `ehem_global_init()` refuses a wolfSSL other than the one the SDK
was built against and returns the new append-only `EHEM_ERR_BACKEND_MISMATCH`;
every implicit-init path (`ehem_ctx_create()` standing up the default
transport, the login flow's self-initialisation) propagates that code
instead of today's `EHEM_ERR_NOMEM`; a hidden seam lets the unit suite
force both failure kinds; a refused init leaves the process-global state
untouched (cleanup a no-op, a later init re-checks). The 1.1.0 "stack
smashing detected" crash (KNOWN-ISSUES) becomes a clean refusal.

**Notes:** Where the work lands — `include/ehem/ehem.h`: `ehem_rc` gains
`EHEM_ERR_BACKEND_MISMATCH` after `EHEM_ERR_UNSUPPORTED` (never
renumbered; REQ-API-003's enum-iterating test covers `ehem_rc_str()`);
`docs/API-GUIDE.md` "Error model" gets the row (the docs gate does not
check enum values, so this is a checklist item). `src/crypto_shim.c`
`ehem_crypto_backend_global_init`: (1) `wolfSSL_lib_version_hex()` vs
`LIBWOLFSSL_VERSION_HEX`; (2) six probes — `Hmac`, `WC_RNG`,
`DecodedCert`, `Aes`, `ed25519_key`, `ed448_key` — each placed inside a
byte buffer with canary bands on both sides sized from the compile-time
`sizeof`, init / one operation with a known answer / free, then canaries
and answer compared; `ecc_key` is library-allocated (`wc_ecc_key_new`) and
needs no probe. A library whose struct is larger than ours overruns into
the canary — exactly the 5.6.6-vs-5.7.2 case (Hmac 896 vs 784 B,
DecodedCert 2576 vs 1528 B). `src/context.c:312-315` and the login flow's
implicit init: carry the real `ehem_rc` out instead of mapping to NOMEM.
Seam: a hidden (non-exported, hidden visibility, absent from
`exports_baseline_1.0.txt`) `ehem__crypto_force_mismatch(int mode)` the
unit tests reach through the static library they already link; mode =
version / probe. Resolve the REQ's OPEN criterion at start and record it
in the REQ (rev bump): `DecodedCert` — recommended a tiny embedded
self-signed DER constant in the shim (no dependency on test fixtures) so
the probe exercises the parse that the SDK actually runs; `WC_RNG` —
`wc_InitRng` + one 16-byte draw + `wc_FreeRng` with canaries only (OS
entropy is involved — an init failure on a healthy system must not read
as a mismatch; a canary change does). Gotchas: no `%zu`/`%lld` anywhere
(no context, no message — the refusal carries no `ehem_last_error`; the
tool composes it from REQ-API-011); ASan must stay clean (the canary
buffers are ours); the dev host's system wolfSSL 5.6.6 is the *foreign*
library — the demo against it is the gate's (STEP-M12-140), the
development build links the system headers so here the "matching" case
is the whole suite. Expect `./dev ci` gcc + clang, `./dev test asan`, the
MinGW syntax check and the export/docs/header gates all green.

**Definition of done**
- [x] `EHEM_ERR_BACKEND_MISMATCH` appended to `ehem_rc`, mapped by
      `ehem_rc_str()`, listed in docs/API-GUIDE.md "Error model".
- [x] Shim: version check + the six guard-banded probes; the matching
      library passes (full unit suite green, gcc + clang + ASan).
- [x] Hidden seam: forced version mismatch and forced probe failure each
      yield `EHEM_ERR_BACKEND_MISMATCH` from `ehem_global_init()`, with no
      wolfCrypt call other than the probes (unit test, `verifies:` tagged).
- [x] Implicit init under a forced mismatch: `ehem_ctx_create()` returns
      `EHEM_ERR_BACKEND_MISMATCH` (not NOMEM); a login over a caller-supplied
      transport likewise (unit tests).
- [x] After a refused init `ehem_global_cleanup()` is a no-op and a following
      `ehem_global_init()` (seam cleared) succeeds — REQ-API-002's
      idempotency test extended.
- [x] REQ-API-010 OPEN criterion (DecodedCert probe input, WC_RNG probe)
      resolved and recorded in the REQ (rev bump); KNOWN-ISSUES entry
      annotated "fixed on development, demo at the M12 gate".
- [x] `implements: REQ-API-010` tag in the shim; MinGW syntax check clean;
      export baseline untouched (no new exported symbol in this step).
