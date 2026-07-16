---
id: STEP-M4-020
title: "crypto shim: local ECDSA/Ed25519 verify + pubkey import (gate enabler)"
milestone: M4
implements: ["REQ-OPS-001"]
traces:
  architecture: ["ARCHITECTURE.md#3-component-overview", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Internal crypto-shim additions that make the M4 gate criterion
executable: import a device-format public key (compressed x963 for NIST
curves per firmware `wc_ecc_export_x963_ex(...,1)`; raw 32-byte Ed25519)
and verify a device-format signature (DER ECDSA-Sig-Value over the
message hashed per alg; raw 64-byte Ed25519 over the message). Internal
only — no new public API; the sign binding's tests and the gate consume it.

**Notes:** Implements the REQ-OPS-001 local-verify acceptance criterion
(infrastructure), like M2-010 did for REQ-AUTH-001. wolfCrypt:
`wc_ecc_import_x963` (+`wc_SignatureVerify` with the alg's hash) and
`wc_ed25519_import_public`/`wc_ed25519_verify_msg`. Compressed-point
import needs `HAVE_COMP_KEY` — confirmed present in the dev-machine
wolfSSL options.h; CHECK the MSYS2 build early (unit tests use fixed
vectors, so a MinGW gap would surface in CI, not only live). Unit vectors:
RFC 8032 §7.1 for Ed25519 verify; a NIST P-256 known-answer vector
(pub + msg + DER sig) for ECDSA — include negative cases (bad sig / wrong
key → clean failure, no crash). Keep wolfSSL headers confined to
crypto_shim.c (header gate).

**Definition of done**
- [ ] `ehem_ecdsa_verify` (curve, compressed-x963 pub, msg, DER sig) and
      `ehem_ed25519_verify` (raw pub, msg, raw sig) in the shim; positive
      + negative unit vectors pass.
- [ ] Compressed-point import proven in a unit test (P-256 vector with a
      compressed pub); MinGW CI leg green (HAVE_COMP_KEY present in MSYS2
      wolfSSL — record the finding).
- [ ] Header gate still rejects wolfSSL types in public headers; ASan/
      LSan clean.
