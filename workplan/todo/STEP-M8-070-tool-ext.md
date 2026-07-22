---
id: STEP-M8-070
title: hem-tool ext pair/list/login — vendored qrcodegen, terminal QR, mobile-login demo
milestone: M8
implements: ["REQ-TOOL-016"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M8-050", "STEP-M8-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the `ext` subcommand family in hem-tool-core: `ext pair`
(full registration orchestration ending in a terminal-rendered UTF-8
half-block QR + bounded `register/check` poll → validate → finalise),
`ext list` (EXTAID-descriptor filter, `[PROTECTED]` marks), `ext login`
(blocking mobile-login demo with distinct exit codes for approved /
rejected / timeout / no-authenticator / unreachable).

**Notes:** vendor Nayuki `qrcodegen` (C, MIT) like cJSON — verbatim
single file + provenance note — compiled ONLY into hem-tool-core (the
export/header gates prove the lib stays clean). QR payload =
`{link, hash:"not_implemented_yet", user, email, hostname}` (test_5
shape); `--no-qr` prints the JSON. Half-block rendering: two modules
per character cell, quiet zone included; fall back to payload print
with a notice if the matrix exceeds the terminal width. Orchestration
in `ext_cmd.{h,c}` (cert_install.c pattern) so the unit test and the
attended run drive the same code. Windows: `_setmode`/console-width
care per the M7-070 lessons. `ext pair` cannot complete unattended —
its unit tests script the broker; the attended completion is M8-080.

**Definition of done**
- [ ] Unit (hem-tool-core): pair leg order + verbatim finalise
      pass-through + exit codes; list filter + protected marks; login
      exit-code matrix (scripted broker)
- [ ] Unit: QR matrix matches a qrcodegen reference fixture; width
      fallback exercised
- [ ] `./dev ci` + asan green on gcc+clang; export/header gates green
      (qrcodegen tool-only); MinGW cross-syntax check clean
- [ ] Tags placed; evidence filled
