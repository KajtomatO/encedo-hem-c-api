---
id: STEP-M6-050
title: "ML-KEM encaps/decaps + ML-DSA sign/verify bindings"
milestone: M6
implements: ["REQ-OPS-007", "REQ-OPS-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: ["6c8a835"]
  tests: ["verifies: REQ-OPS-007, REQ-OPS-008 (tests/unit/test_pqc.c — 6 cases; tests/integration/test_pqc_live.c — 2 cases live green 2026-07-17)"]
  notes: >
    Unit 27/27 gcc+clang + ASan clean; export/header gates green. Live
    (my.ence.do fw v1.2.2-DIAG): MLKEM768 encaps→decaps same ss (ct 1088),
    truncated ct → 406, MLKEM512 ct 768; MLDSA65 sign→verify (sig 3309),
    ctx round-trip + cross-ctx fail, MLDSA44 sig 2420. BOTH PROBES
    RESOLVED: (1) decaps alg = "keymgmt:use:2cd…" — the handler echoes its
    scope-check SCRATCH buffer (unwritten-param bug confirmed + minor
    response-content leak; REQ-OPS-007 rev2); (2) invalid ML-DSA verify →
    HTTP status 795 (raw fw error code, not doc's 406; REQ-OPS-008 rev2)
    — SDK's defensive any-non-200→DEVICE mapping worked, unit tests pin
    65307/−229/100 as DEVICE. Both = upstream filings at the gate.
reopened: []
cancelled: null
---

**Goal:** Four PQC bindings in proto_crypto.c + crypto.h:
`ehem_mlkem_encaps` (→ {alg, ss, ct}, ss zeroized on free),
`ehem_mlkem_decaps` (→ {ss}), `ehem_mldsa_sign` (→ {alg, sig}),
`ehem_mldsa_verify` (empty-200 → OK). Live KEM round-trip (encaps→decaps
same ss), ML-DSA sign→verify ± ctx, and the two firmware-quirk probes
(decaps alg garbage, mldsa-verify garbage HTTP status).

**Notes:** All four are small kid(+payload) POSTs — one step. Tolerant
parse on `alg` everywhere (decaps echoes an unwritten buffer, REQ-OPS-007).
The mldsa-verify failure path must survive an out-of-range HTTP status
(fw returns SIG_VERIFY_E raw, printed unsigned ≈ 65307) — check
transport_curl/proto_common don't choke before adding the unit case;
map any non-200 (except 403) to EHEM_ERR_DEVICE with the raw status
retrievable. ML-DSA keygen is slow — reuse the M5 matrix timeout support
(ehem_test_ctx_timeout 120s) and MATRIX-style transient retry.

**Definition of done**
- [x] Four functions (+ `_free`s, ss zeroized) exported, tagged
      `implements: REQ-OPS-007` / `REQ-OPS-008`
- [x] Unit tests green (gcc+clang+asan): body bytes ({kid} only for
      encaps; ct/msg/ctx/sign b64 fields exactly when given), ss/ct/sig
      decode, alg tolerated absent, out-of-range verify status →
      EHEM_ERR_DEVICE (no crash/PROTOCOL), EHEM_ERR_ARG guards (ct >
      1568, sig > 4627, ctx > 255, msg bounds) with zero I/O, 400/403/406
      mapping, token sharing
- [x] Live ML-KEM: 768 key encaps → 32B ss + 1088B ct + alg "MLKEM768",
      decaps(ct) → same ss; truncated ct → 406; MLKEM512 round-tripped
      (ct 768); decaps alg value recorded in REQ-OPS-007 (scope-buffer
      echo confirmed)
- [x] Live ML-DSA: 65 key sign → 3309B sig + alg "MLDSA65", device verify
      OK; ctx round-trip + cross-ctx verify fails; MLDSA44 round-tripped
      (sig 2420); invalid-sig HTTP status 795 recorded in REQ-OPS-008
      (open criterion resolved); cleanup per REQ-TEST-003
- [x] Export + public-header gates green
