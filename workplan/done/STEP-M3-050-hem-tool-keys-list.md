---
id: STEP-M3-050
title: "hem-tool-core: protected-key classifier + keys list subcommand"
milestone: M3
implements: ["REQ-TOOL-005", "REQ-TOOL-004"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M3-010"]
evidence:
  commits:
    - "47b4b16 — hem-tool keys list + protected-key classifier (REQ-TOOL-004/005)"
  tests:
    - "tests/unit/test_keys.c — classifier table (exact TLS labels; near-misses tls privatekey / 'TLS PrivateKey 2' / trailing-space unprotected; (Android)/(iPhone) any case+position protected; ordinary/NULL unprotected — 17 rows), keys list multi-page marks+counts (12 keys/2 pages, 3 protected, summary), read-only (4 requests = login+2 list), no-passphrase → exit 2 (no I/O), auth-failure → exit 1 (4 cases)"
  notes: >
    New src/tools/hem-tool/keys.{c,h} in hem-tool-core. hem_key_is_protected()
    is the single label-based classifier (exact "TLS PrivateKey"/"TLS Certificate"
    via strcmp; "(android)"/"(iphone)" via a local case-insensitive ci_contains) —
    grep confirms the label constants live only there in production code; keys rm
    (M3-060) will reuse it. hem_keys_list_run(ctx, opts) logs in, walks the repo
    (ehem_key_list_all, read-only), prints "  <kid>  '<label>'  (<type>)[  [PROTECTED]]"
    per key + a "<n> key(s), <p> protected" summary to a caller FILE* (tmpfile()
    in the test, not open_memstream — M2-070 MinGW lesson). main.c gains a two-word
    `keys` dispatch (subcmd) + `keys list`; exit 0/1/2 (usage=2, runtime=1).
    LIVE (./dev tool keys list, my.ence.do, 2026-07-16):
      e16f3606...  'TLS PrivateKey'      (PKEY,GENERIC_DER)          [PROTECTED]
      e68c4e0d...  'SM-S938B (Android)'  (ECDH,CURVE25519)           [PROTECTED]
      c9071262...  'Encedo OIDC - tomasz'(ATT,PKEY,ExDSA,SECP256R1)
      ce89cc10...  'it-ecdh-1'           (ATT,PKEY,ECDH,ExDSA,SECP256R1)
      e6d0ed9c...  'it-ecdh-2'           (ATT,PKEY,ECDH,ExDSA,SECP256R1)
      9b1e83ef...  'TLS Certificate'     (CERT,GENERIC_DER)          [PROTECTED]
      -> 6 key(s), 3 protected  (TLS pair + paired Android phone marked)
    Unit 17/17 gcc+clang + ASan/LSan clean; MinGW cross-compile of keys.c+main.c
    clean; export/header gates green.
reopened: []
cancelled: null
---

**Goal:** In the shared hem-tool-core code (cert_install.c pattern): the
protected-key classifier — label exactly `TLS PrivateKey` /
`TLS Certificate`, or containing `(Android)` / `(iPhone)`
case-insensitively → protected — and a `keys list` implementation that
walks the full repo (REQ-KEY-001), prints kid/label/type per key with
`[PROTECTED]` marks and a summary line (total + protected counts),
mirroring `wipe_keys.py --list`. Wired as `hem-tool keys list` in
main.c with the existing URL/passphrase conventions.

**Notes:** One classifier function, used by both keys subcommands and the
tests — no duplicated label constants (REQ-TOOL-005 grep criterion).
Classification is label-only by design (same algorithm legitimately
appears on non-protected keys). Strictly read-only: auth + list requests
only. Exit codes: 0 success, 2 usage/env (missing URL/passphrase), 1
runtime failure with the mapped error text. New keys.c/keys.h beside
cert_install.c in the hem-tool-core lib so the unit test drives the same
code the CLI runs; output to a caller-supplied FILE* for testability
(portable tmpfile(), not open_memstream — M2-070 MinGW lesson).

**Definition of done**
- [x] Classifier table-test: exact TLS labels; near-misses
      (`tls privatekey`, `TLS PrivateKey 2`) unprotected;
      `(Android)`/`(iPhone)` any case/position protected; ordinary labels
      unprotected. (test_classifier_table.)
- [x] Unit (fake transport): multi-page repo prints every key once with
      correct marks + counts; request sequence is auth + list only;
      missing env → exit 2; auth failure → exit 1. (test_keys_list_* — 3
      tests; read-only asserted via 4-request count.)
- [x] One shared classifier symbol — no duplicated label constants (grep).
      (hem_key_is_protected; grep clean — constants only in keys.c.)
- [x] Live: `hem-tool keys list` against the dev device shows its keys
      with the TLS pair marked `[PROTECTED]` (manual run recorded in
      evidence; full demo at the M3 gate). (Run recorded in Notes — TLS pair
      + `SM-S938B (Android)` marked; "6 key(s), 3 protected".)
