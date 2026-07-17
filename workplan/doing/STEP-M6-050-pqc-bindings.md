---
id: STEP-M6-050
title: "ML-KEM encaps/decaps + ML-DSA sign/verify bindings"
milestone: M6
implements: ["REQ-OPS-007", "REQ-OPS-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Four functions (+ `_free`s, ss zeroized) exported, tagged
      `implements: REQ-OPS-007` / `REQ-OPS-008`
- [ ] Unit tests green (gcc+clang+asan): body bytes ({kid} only for
      encaps; ct/msg/ctx/sign b64 fields exactly when given), ss/ct/sig
      decode, alg tolerated absent, out-of-range verify status →
      EHEM_ERR_DEVICE (no crash/PROTOCOL), EHEM_ERR_ARG guards (ct >
      1568, sig > 4627, ctx > 255, msg bounds) with zero I/O, 400/403/406
      mapping, token sharing
- [ ] Live ML-KEM: 768 key encaps → 32B ss + 1088B ct + alg "MLKEM768",
      decaps(ct) → same ss; truncated ct → 406; one more set (512 or
      1024) round-tripped; decaps alg field value recorded in REQ-OPS-007
- [ ] Live ML-DSA: 65 key sign → 3309B sig + alg "MLDSA65", device verify
      OK; ctx round-trip + cross-ctx verify fails; one more set
      round-tripped; invalid-sig HTTP status recorded in REQ-OPS-008
      (open criterion resolved); cleanup per REQ-TEST-003
- [ ] Export + public-header gates green
