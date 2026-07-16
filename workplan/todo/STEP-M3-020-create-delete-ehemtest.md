---
id: STEP-M3-020
title: "create + delete bindings; EHEMTEST test support; live create→list→delete round-trip"
milestone: M3
implements: ["REQ-KEY-005", "REQ-KEY-004", "REQ-TEST-003"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M3-010"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Unit: create body byte-exact `{type,label[,mode][,descr(b64)]}`;
      kid parsed; label >31B / non-printable → EHEM_ERR_ARG without
      transport call; 400/406 → EHEM_ERR_DEVICE with payload; scope
      `keymgmt:gen` declared.
- [ ] Unit: delete sends DELETE + kid path, no body; empty-200 → EHEM_OK;
      malformed kid → EHEM_ERR_ARG without transport call; 406 →
      EHEM_ERR_NOT_FOUND; scope `keymgmt:del` declared.
- [ ] EHEMTEST support helper: unique labels, registered cleanup running
      pass-or-fail, prefix sweep; no integration test creates a
      non-EHEMTEST label (grep).
- [ ] `_free` NULL-safe; ASan/LSan clean; export + header gates green.
- [ ] Live: create EHEMTEST ED25519 key with descr → kid in list with
      that label/descr → delete → gone; second delete → NOT_FOUND
      (integration test, gated).
