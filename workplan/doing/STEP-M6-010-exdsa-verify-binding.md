---
id: STEP-M6-010
title: "ehem_verify — /api/crypto/exdsa/verify binding"
milestone: M6
implements: ["REQ-OPS-003"]
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

**Goal:** `ehem_verify(ctx, kid, alg, msg, msg_len, sig_ctx, sig_ctx_len,
sig, sig_len)` in proto_crypto.c + crypto.h: device-side ExDSA signature
verification; `EHEM_OK` = valid (empty-body 200), 406 → `EHEM_ERR_DEVICE`.
Unit tests (fake transport, body bytes, error mapping, shared per-KID
token with sign) + live sign→verify round-trip incl. tampered-signature
negative.

**Notes:** Mirror of the M4 sign binding — same alg literals, pre-validation
limits (sig ≤ 148 decoded is the one new bound), and the empty-2xx success
pattern already used by reboot/delete. Reuses test_sign_live's key setup;
verify shares the sign step's `keymgmt:use:<kid>` cached token (assert
request counts like test_sign_shares_get_token).

**Definition of done**
- [ ] `ehem_verify` exported, tagged `implements: REQ-OPS-003`; header docs
      state 406 = invalid-sig/wrong-type/not-found (indistinguishable)
- [ ] Unit tests green (gcc+clang+asan): byte-exact bodies with/without
      ctx, empty-200 → OK, 400/403/406 mapping, EHEM_ERR_ARG guards with
      zero I/O, no second token acquisition after sign
- [ ] Live: sign→verify OK; flipped-bit sig and truncated msg → 406 /
      EHEM_ERR_DEVICE (http_status recorded); EHEMTEST cleanup per
      REQ-TEST-003
- [ ] Export + public-header gates green
