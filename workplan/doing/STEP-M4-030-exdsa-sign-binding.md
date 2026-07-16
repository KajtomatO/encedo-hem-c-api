---
id: STEP-M4-030
title: "ehem_sign binding — /api/crypto/exdsa/sign + live scope probe (risk 3)"
milestone: M4
implements: ["REQ-OPS-001"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M4-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Public `ehem_sign(ctx, kid, alg, msg, msg_len, sig_ctx,
sig_ctx_len, &out)` in new `proto_crypto.c` + `include/ehem/crypto.h`
over POST /api/crypto/exdsa/sign: body `{kid, msg(b64), alg[, ctx(b64)]}`,
scope exact `keymgmt:use:<kid>` (same cache entry as key-get), response
`sign` decoded to caller-owned bytes preserved verbatim (ECDSA = DER,
EdDSA = raw). Alg selector constants for the 8 firmware labels. Live:
sign with created EHEMTEST keys verifies locally via the M4-020 shim.

**Notes:** First `crypto` API-group module (ARCH §6 layout). Firmware
facts (fw v1.2.2 api_crypto.c / crypto.c): scope is a plain strcmp — no
prefix scopes, sub!=M; device hashes the full message internally in all
variants (msg ≤ 2048, len 0 rejected by CRYPTO_Sign); 406 is ambiguous
(kid not found / wrong key type for alg / crypto failure) → EHEM_ERR_DEVICE,
NOT NOT_FOUND. Pre-validate client-side (ARG, no I/O): kid 32-hex, empty
msg, msg > 2048, sig_ctx > 255. ECDSA live target needs create with
mode="ExDSA" (device defaults ECDH-only → sign 406, api_quirks). LIVE
SCOPE PROBE while here (closes §12 risk 3): sign with a `keymgmt:get`-
scoped token → expect 403; sign with the per-KID token acquired by a
prior get → expect 200 with no second acquisition. Record results in
REQ-OPS-001 + §12 risk 3 (final RESOLVED text lands at the gate).

**Definition of done**
- [ ] Unit: byte-exact request body with/without ctx; sign decoded
      (padded base64); `_free` NULL-safe; unknown response fields ignored.
- [ ] Unit: exact scope `keymgmt:use:<kid>`; get-then-sign on one kid =
      ONE token acquisition (request-count assert); two kids = two.
- [ ] Unit: ARG pre-validation with zero transport calls; 403 →
      SCOPE_DENIED; 400/406 → EHEM_ERR_DEVICE.
- [ ] Live (integration, gated): EHEMTEST SECP256R1 (mode ExDSA) →
      SHA256WithECDSA DER sig verifies via shim against compressed-x963
      pub from get; EHEMTEST ED25519 → Ed25519 64-byte sig verifies;
      cleanup per REQ-TEST-003.
- [ ] Live scope probe recorded in REQ-OPS-001 open criterion + §12
      risk 3 notes.
- [ ] Export + header gates green; MinGW clean; ASan/LSan clean.
