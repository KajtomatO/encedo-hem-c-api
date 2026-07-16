---
id: STEP-M3-050
title: "hem-tool-core: protected-key classifier + keys list subcommand"
milestone: M3
implements: ["REQ-TOOL-005", "REQ-TOOL-004"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M3-010"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Classifier table-test: exact TLS labels; near-misses
      (`tls privatekey`, `TLS PrivateKey 2`) unprotected;
      `(Android)`/`(iPhone)` any case/position protected; ordinary labels
      unprotected.
- [ ] Unit (fake transport): multi-page repo prints every key once with
      correct marks + counts; request sequence is auth + list only;
      missing env → exit 2; auth failure → exit 1.
- [ ] One shared classifier symbol — no duplicated label constants (grep).
- [ ] Live: `hem-tool keys list` against the dev device shows its keys
      with the TLS pair marked `[PROTECTED]` (manual run recorded in
      evidence; full demo at the M3 gate).
