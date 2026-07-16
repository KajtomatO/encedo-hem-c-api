---
id: STEP-M5-030
title: "hem-tool keys gen — create subcommand with NIST mode default"
milestone: M5
implements: ["REQ-TOOL-009"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits:
    - "a9dc42f — hem-tool keys gen + unit tests (live demo pending device)"
  tests:
    - "test_keys_gen (verifies: REQ-TOOL-009): 7 cases — explicit --mode verbatim (body bytes + 3 requests), NIST default ECDH,ExDSA + stderr note, non-NIST omits mode, --descr base64 (abc→YWJj), usage errors (no type/label/passphrase, bad mode literal) → exit 2 with 0 transport I/O, SDK label-bound EHEM_ERR_ARG → exit 2 no create, device 400/406 → exit 1. gcc+clang + asan green; export/header gates green"
  notes: >
    Code complete + unit-green. hem_keys_gen_run in hem-tool-core
    (src/tools/hem-tool/keys.c); main.c dispatch for `keys gen TYPE` +
    --label/--descr/--mode flags + usage text. Tool-level NIST-P/K default
    mode ECDH,ExDSA (via ehem_key_type_parse, offline) so a generated NIST key
    can sign; SDK stays verbatim (REQ-KEY-005). TYPE passthrough (no allowlist);
    label/descr validation delegated to the SDK (EHEM_ERR_ARG → exit 2), so this
    step is independent of M5-010's bound change. `keys gen` usage-error exit
    codes verified live-locally (no device needed — validation precedes login).
    LIVE DEMO (2026-07-17, my.ence.do): `keys gen ED25519 --label 'EHEMTEST
    m5gen ed'` and `keys gen SECP256R1 --label 'EHEMTEST m5gen ec'` created keys
    (kids e6fc7d44… / 3a9a0f08…); the SECP256R1 run printed the ECDH,ExDSA
    default-mode note, ED25519 printed none. Both appeared in `keys list`
    (`ATT,PKEY,ExDSA,ED25519` / `ATT,PKEY,ECDH,ExDSA,SECP256R1`), both signed
    via `sign` with the default-alg lookup (Ed25519 / SHA256WithECDSA notes;
    exit 0), `keys pub` printed the ED25519 metadata + pubkey; cleaned up via
    `keys rm --label-prefix 'EHEMTEST m5gen' --yes` (2 deleted, 0 protected).
reopened: []
cancelled: null
---

**Goal:** `hem-tool keys gen <TYPE> --label LABEL [--descr STR]
[--mode ECDH|ExDSA|ECDH,ExDSA]` working end to end: creates via
`ehem_key_create`, prints the new kid to stdout; NIST-P/K types without
`--mode` get the tool-level `ECDH,ExDSA` default with a stderr note
(REQ-TOOL-009); exit 0/1/2 per tool conventions. Orchestration in
`hem-tool-core` (`src/tools/hem-tool/keys.{h,c}` alongside the other
keys subcommands), unit tests in `tests/unit/test_keys_gen.c` (fake
transport, body-byte and request-sequence asserts), main.c dispatch +
usage text extended.

**Notes:** TYPE passthrough — no tool-side allowlist (REQ-KEY-005; the
NIST-default classification uses `ehem_key_type_parse` on the TYPE
string, which is offline/pure). Label/descr validation is delegated to
the SDK (`EHEM_ERR_ARG` → exit 2); no duplicated bounds in the tool, so
this step is independent of STEP-M5-010's bound change. `--mode` literal
validated tool-side against the exact three strings (anything else is a
guaranteed device 400 — fail fast as usage, exit 2). Live demo: gen →
list → sign → pub → rm chain with an EHEMTEST label, noted in evidence
(feeds the REQ's live criterion, checked at the gate).

**Definition of done**
- [x] Unit tests green (gcc+clang + asan): body bytes for all option
      combinations, NIST default-mode injection + note, non-NIST omits
      mode, verbatim `--mode` passthrough, usage errors exit 2 with zero
      transport I/O, device 400/406 → exit 1. — test_keys_gen (7 cases).
- [x] Live demo performed and recorded (EHEMTEST keys created, used by
      `sign`, `pub`'d, removed via `keys rm`) — see evidence notes.
- [x] Usage/help text updated; README lists no subcommands (only a
      `status` example), so nothing to update there.
- [x] `./dev ci` green; export/header gates green.
