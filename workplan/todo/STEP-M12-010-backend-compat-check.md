---
id: STEP-M12-010
title: "SDK crypto-backend compatibility check at ehem_global_init — EHEM_ERR_BACKEND_MISMATCH, wolfSSL version check + guard-banded self-test, no NOMEM masking, hidden test seam"
milestone: M12
implements: ["REQ-API-010"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `EHEM_ERR_BACKEND_MISMATCH` appended to `ehem_rc`, mapped by
      `ehem_rc_str()`, listed in docs/API-GUIDE.md "Error model".
- [ ] Shim: version check + the six guard-banded probes; the matching
      library passes (full unit suite green, gcc + clang + ASan).
- [ ] Hidden seam: forced version mismatch and forced probe failure each
      yield `EHEM_ERR_BACKEND_MISMATCH` from `ehem_global_init()`, with no
      wolfCrypt call other than the probes (unit test, `verifies:` tagged).
- [ ] Implicit init under a forced mismatch: `ehem_ctx_create()` returns
      `EHEM_ERR_BACKEND_MISMATCH` (not NOMEM); a login over a caller-supplied
      transport likewise (unit tests).
- [ ] After a refused init `ehem_global_cleanup()` is a no-op and a following
      `ehem_global_init()` (seam cleared) succeeds — REQ-API-002's
      idempotency test extended.
- [ ] REQ-API-010 OPEN criterion (DecodedCert probe input, WC_RNG probe)
      resolved and recorded in the REQ (rev bump); KNOWN-ISSUES entry
      annotated "fixed on development, demo at the M12 gate".
- [ ] `implements: REQ-API-010` tag in the shim; MinGW syntax check clean;
      export baseline untouched (no new exported symbol in this step).
