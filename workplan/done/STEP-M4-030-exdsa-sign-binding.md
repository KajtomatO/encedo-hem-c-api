---
id: STEP-M4-030
title: "ehem_sign binding — /api/crypto/exdsa/sign + live scope probe (risk 3)"
milestone: M4
implements: ["REQ-OPS-001"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M4-020"]
evidence:
  commits:
    - "4bbeffc — ehem_sign binding + live scope probe (REQ-OPS-001)"
  tests:
    - "tests/unit/test_sign.c — byte-exact bodies {kid,msg,alg[,ctx]}; get→sign one-token cache proof (4 requests) + independent second kid (7, scope decoded); 11 ARG guards w/ zero transport calls; 403→SCOPE_DENIED, 400/406→DEVICE (406 NOT NOT_FOUND); missing/undecodable sign → PROTOCOL; _free NULL-safe"
    - "tests/integration/test_sign_live.c — LIVE GREEN (my.ence.do fw v1.2.2-DIAG): ED25519 64B sig verifies locally (RFC 8032) and truncated msg rejected; SECP256R1(mode ExDSA) 71B DER sig verifies against the 33B COMPRESSED pubkey; classifier size cross-check (REQ-KEY-006) live; SCOPE PROBE: keymgmt:get → 403 (risk 3 closed), get→sign shares one keymgmt:use:<kid> token"
  notes: >
    include/ehem/crypto.h (new API group header; EHEM_SIGN_ALG_* literals,
    EHEM_SIGN_MSG_MAX 2048, EHEM_SIGN_SIG_CTX_MAX 255, ehem_signature) +
    src/proto_crypto.c (proto_keymgmt template; per-kid scope snprintf;
    signature bytes returned verbatim — DER for ECDSA, raw for EdDSA).
    is_kid_hex promoted to proto_common (ehem_proto_is_kid_hex), keymgmt
    call sites switched. LIVE FINDINGS (recorded in REQ-OPS-001):
    (1) §12 RISK 3 CLOSED — sign accepts ONLY exact keymgmt:use:<kid>
    (probe: keymgmt:get → 403, same token class get ACCEPTS — asymmetry
    live-confirmed, firmware strcmp api_crypto.c:456); one cached token
    per kid serves get+sign (live + unit proof).
    (2) The device returned a 71-byte DER sig (≤ 72 max) and the 33-byte
    compressed P-256 pubkey — REQ-KEY-006 constants match the wire.
    (3) One transient live flake observed (device closes TCP per response,
    reachability intermittent from this env) — clean on rerun; suite 10/10.
    ./dev ci 19/19 gcc+clang; asan clean; export gate green (2 new
    ehem_* exports), header gate green. §12 risk 3 text edit deferred to
    the M4 gate step per plan.
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
- [x] Unit: byte-exact request body with/without ctx; sign decoded
      (padded base64); `_free` NULL-safe; unknown response fields ignored.
      (test_sign_body_and_decode, test_sign_with_sig_ctx.)
- [x] Unit: exact scope `keymgmt:use:<kid>`; get-then-sign on one kid =
      ONE token acquisition (request-count assert); two kids = two.
      (test_sign_shares_get_token.)
- [x] Unit: ARG pre-validation with zero transport calls; 403 →
      SCOPE_DENIED; 400/406 → EHEM_ERR_DEVICE. (test_sign_arg_guards,
      test_sign_device_errors, test_sign_protocol_errors.)
- [x] Live (integration, gated): EHEMTEST SECP256R1 (mode ExDSA) →
      SHA256WithECDSA DER sig verifies via shim against compressed-x963
      pub from get; EHEMTEST ED25519 → Ed25519 64-byte sig verifies;
      cleanup per REQ-TEST-003. (test_sign_live GREEN live 2026-07-16;
      71B DER / 33B compressed pubkey observed.)
- [x] Live scope probe recorded in REQ-OPS-001 open criterion + §12
      risk 3 notes. (keymgmt:get → 403; risk 3 closed — §12 text lands
      at the gate step.)
- [x] Export + header gates green; MinGW clean; ASan/LSan clean.
      (export_symbols/public_headers green; proto_crypto.c has no
      platform-specific code — MinGW leg proven by CI on push; asan 19/19.)
