---
id: STEP-M5-020
title: "Per-family generation matrix (21 types) + shim ehem_ed448_verify"
milestone: M5
implements: ["REQ-TEST-004"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `tests/integration/test_keygen_matrix_live.c` — table-driven
over all 21 fw v1.2.2 create types (REQ-TEST-004 list). Per family:
create (EHEMTEST label; NIST-P/K with mode `ECDH,ExDSA`) → full-walk
list membership + live flag-set capture → get + REQ-KEY-006 classifier
cross-check (family, pubkey length where asymmetric) → sign + local shim
verify for the six ExDSA-capable families → delete → absence check.
Sequential, ≤ 1 matrix key alive at a time; mid-cycle failure still
attempts deletion. Crypto shim gains `ehem_ed448_verify` (public 57 B,
sig 114 B; `wc_ed448_verify_msg`; NOT_COMPILED_IN → EHEM_ERR_UNSUPPORTED
like the compressed-point pattern) with an RFC 8032 §7.4 unit vector in
test_crypto.c.

**Notes:** Both Debian and MSYS2 wolfSSL builds have HAVE_ED448
(verified 2026-07-16: /usr/include/wolfssl/options.h and the extracted
mingw-w64-x86_64-wolfssl-5.9.2-2 options.h) — the UNSUPPORTED fallback
covers exotic builds only. Ed25519/Ed448 device signs use the pure
`Ed25519`/`Ed448` selectors (device hashes internally; REQ-OPS-001).
Use a generous per-request timeout in the test ctx options — ML-DSA
keygen on the MCU may be slow; record observed timings in evidence.
Known transient flake: device closes TCP after every response.
Flag-set vocabulary observed for AES/HMAC/MLKEM/MLDSA is recorded into
REQ-KEY-006 (its open criterion is *checked off at the M5 gate*, not
here, so the gate run confirms). Classifier token additions only if the
live vocabulary demands them (tolerant-parse rule).

**Definition of done**
- [ ] Matrix test exists, table-driven, 21 rows, `integration` label,
      skips (77) without EHEM_TEST_URL/PASSPHRASE.
- [ ] Live run green against the dev device; per-family flag-sets and
      timings captured in evidence notes.
- [ ] Sign+local-verify green for SECP256R1/384R1/521R1/256K1 (DER sigs)
      and ED25519/ED448 (raw 64/114 B) via the shim.
- [ ] `ehem_ed448_verify` unit vector (RFC 8032 §7.4) green gcc+clang +
      asan; UNSUPPORTED path records-not-fails.
- [ ] No stranded EHEMTEST keys after green and after forced-failure
      dry-run (cleanup leg exercised).
- [ ] `./dev ci` green; export/header gates green (no new public API —
      shim stays internal).
