---
id: STEP-M5-030
title: "hem-tool keys gen — create subcommand with NIST mode default"
milestone: M5
implements: ["REQ-TOOL-009"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Unit tests green (gcc+clang + asan): body bytes for all option
      combinations, NIST default-mode injection + note, non-NIST omits
      mode, verbatim `--mode` passthrough, usage errors exit 2 with zero
      transport I/O, device 400/406 → exit 1.
- [ ] Live demo performed and recorded (EHEMTEST key created, used by
      `sign`, removed via `keys rm`).
- [ ] Usage/help text updated; README tool section updated if it lists
      subcommands.
- [ ] `./dev ci` green; export/header gates green.
