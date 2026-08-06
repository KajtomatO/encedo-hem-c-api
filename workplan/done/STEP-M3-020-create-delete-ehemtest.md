---
id: STEP-M3-020
title: "create + delete bindings; EHEMTEST test support; live create→list→delete round-trip"
milestone: M3
implements: ["REQ-KEY-005", "REQ-KEY-004", "REQ-TEST-003"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M3-010"]
evidence:
  commits:
    - "8c3fb80 — keymgmt create/delete (REQ-KEY-004/005, REQ-TEST-003); fix device no-keep-alive transport (CURLOPT_FORBID_REUSE, REQ-NET-002)"
  tests:
    - "tests/unit/test_keymgmt.c — create: byte-exact body {type,label[,mode][,descr(b64)]}, kid parsed, label >31/non-printable → ARG (0 requests), 400/406 → DEVICE w/ payload, missing kid → PROTOCOL, scope keymgmt:gen (eJWT decode), arg guards; delete: DELETE+kid path/no body, empty-200 → OK, 200-with-body → OK, malformed kid (short/33/non-hex/NULL) → ARG (0 requests), 406 → NOT_FOUND, 403 → SCOPE_DENIED, scope keymgmt:del (eJWT decode) (25 cases total incl. M3-010 list)"
    - "tests/integration/test_keymgmt_mutate_live.c — live create EHEMTEST ED25519 + descr → found in list_all w/ label+descr (exact bytes) → delete → gone → second delete → NOT_FOUND; setup sweep + teardown cleanup (pass-or-fail)"
    - "tests/support/ehem_test_keys.h — EHEMTEST label mint / track / cleanup / prefix-sweep (REQ-TEST-003), public-API-only header"
  notes: >
    ehem_key_create(ctx, params, kid_out): POST /api/keymgmt/create scope
    keymgmt:gen; params {type,label,mode?,descr?/len}; label pre-validated
    1..31 printable-ASCII (→ EHEM_ERR_ARG, no I/O); type/mode passthrough (no
    allowlist/default); descr SDK std-base64-encodes (omitted when len 0); body
    keys emitted type,label,mode,descr in order; kid parsed + validated 32-hex
    into a caller EHEM_KID_HEX_SIZE(33) buffer (missing/!hex → PROTOCOL).
    ehem_key_delete(ctx, kid): DELETE /api/keymgmt/delete/{kid} scope keymgmt:del;
    kid pre-validated 32-hex (→ ARG, no I/O); empty-200 handled as success via
    the shared path's empty-2xx PROTOCOL/200 signal (like reboot); 406 remapped
    to NOT_FOUND (payload stack-copied first — ehem_ctx_fail frees err_payload
    before copying).
    TRANSPORT FIX (REQ-NET-002): the HEM device closes TCP after every response
    (no keep-alive; the python client forces Connection: close). curl was pooling
    connections, so DELETE reused a dead socket and curl's retry exhausted
    ("Connection died, tried 5 times"). Fixed with CURLOPT_FORBID_REUSE=1 at
    handle setup — every request now dials fresh. The deleted key was reaching
    the device regardless (no leak), but the response was unreadable. All
    integration tests still green after the fix (regression covered by the live
    round-trip); recorded as a device quirk.
    Unit 25/25 gcc+clang + ASan/LSan clean; export/header gates green; full
    integration suite 6/6 green (incl. the new mutate round-trip).
reopened: []
cancelled: null
---

**Goal:** `ehem_key_create(ctx, params, &kid_out)` (POST /api/keymgmt/create,
scope `keymgmt:gen`) and `ehem_key_delete(ctx, kid)` (DELETE
/api/keymgmt/delete/{kid}, scope `keymgmt:del`) in proto_keymgmt.c; a
tests/support helper that mints unique `EHEMTEST`-prefixed labels and
registers created kids for teardown cleanup (REQ-TEST-003); an
integration test doing create → find in list → delete → verify gone,
end to end against the live device.

**Notes:** Create passes `type` through verbatim (no SDK allowlist —
device 400 with payload decides); label pre-validated printable + ≤31
bytes (stricter of the conflicting doc-32/python-31 bounds, open
criterion in REQ-KEY-005); `mode` passthrough with NO default (device
defaults NIST-P to ECDH-only — caller's choice); `descr` raw bytes,
SDK base64-encodes, no client cap (64-vs-128 conflict open). Delete:
kid pre-validated (32 hex chars → else EHEM_ERR_ARG, no I/O); success is
HTTP 200 with an EMPTY body — accept the shared path's empty-2xx
PROTOCOL signal as success exactly like the reboot binding; 406 →
EHEM_ERR_NOT_FOUND. Transport already supports DELETE with sticky-state
reset (transport_curl.c reset_method — verified at decomposition).
Cleanup helper must run on test failure too, and a prefix sweep clears
leftover EHEMTEST keys from interrupted runs. Live type: ED25519 (no
mode quirk, 32-byte pubkey for M3-040 reuse).

**Definition of done**
- [x] Unit: create body byte-exact `{type,label[,mode][,descr(b64)]}`;
      kid parsed; label >31B / non-printable → EHEM_ERR_ARG without
      transport call; 400/406 → EHEM_ERR_DEVICE with payload; scope
      `keymgmt:gen` declared. (test_create_* — 8 cases; scope asserted by
      decoding the token eJWT.)
- [x] Unit: delete sends DELETE + kid path, no body; empty-200 → EHEM_OK;
      malformed kid → EHEM_ERR_ARG without transport call; 406 →
      EHEM_ERR_NOT_FOUND; scope `keymgmt:del` declared. (test_delete_* — 5
      cases.)
- [x] EHEMTEST support helper: unique labels, registered cleanup running
      pass-or-fail, prefix sweep; no integration test creates a
      non-EHEMTEST label (grep). (tests/support/ehem_test_keys.h; the only
      key-creating test uses ehem_test_label — grep clean.)
- [x] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
      (ehem_key_page_free(NULL) tested; ./dev test asan + ./dev check green.)
- [x] Live: create EHEMTEST ED25519 key with descr → kid in list with
      that label/descr → delete → gone; second delete → NOT_FOUND
      (integration test, gated). (test_keymgmt_mutate_live — passed live;
      exposed + fixed the device no-keep-alive transport bug, see Notes.)
