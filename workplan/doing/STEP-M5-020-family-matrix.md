---
id: STEP-M5-020
title: "Per-family generation matrix (21 types) + shim ehem_ed448_verify"
milestone: M5
implements: ["REQ-TEST-004"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits:
    - "63fe2b5 — ed448 verify + per-family matrix test (live run pending device)"
  tests:
    - "test_crypto (verifies: REQ-TEST-004): test_ed448_verify — RFC 8032 §7.4 vectors (empty + 1-byte 0x03) verify via wc_ed448; tamper/wrong-msg → clean invalid; arg validation; UNSUPPORTED path records-not-fails. gcc+clang + asan green"
    - "test_keygen_matrix_live (verifies: REQ-TEST-004): builds + links; LIVE RUN PENDING — device my.ence.do offline during this session (see notes)"
  notes: >
    Code complete + unit-green. ehem_ed448_verify added to the crypto shim
    (public 57 / sig 114; NOT_COMPILED_IN → EHEM_ERR_UNSUPPORTED); RFC 8032
    §7.4 unit vectors pass against wolfCrypt on gcc+clang+asan. The
    integration matrix test_keygen_matrix_live covers all 21 fw v1.2.2 create
    types, table-driven, with per-op transient-network retry (the device
    closes TCP per response + has intermittent reachability; the SDK does no
    retries by design, §7, so the test tolerates flakes). Support helpers
    added: ehem_test_ctx_timeout (120 s for slow ML-DSA keygen),
    ehem_test_untrack.
    LIVE STATUS: partial runs earlier this session PROVED the logic — SECP256R1
    ran the full cycle (create → list [flag-set "ATT,PKEY,ECDH,ExDSA,SECP256R1"]
    → get + classifier cross-check pubkey_len=33 → sign → 71-byte DER verified
    locally → delete) and SECP384R1 classified (pubkey_len=49) — before the dev
    device went fully offline (connect refused to port 443, confirmed with a
    direct curl; ~20+ min outage). The FULL green matrix run + the
    AES/HMAC/MLKEM/MLDSA flag-set vocabulary capture are DEFERRED to when the
    device is reachable (a background monitor is watching); this step stays in
    doing/ until then.
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
