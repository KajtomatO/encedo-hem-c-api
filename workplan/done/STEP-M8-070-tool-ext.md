---
id: STEP-M8-070
title: hem-tool ext pair/list/login — vendored qrcodegen, terminal QR, mobile-login demo
milestone: M8
implements: ["REQ-TOOL-016"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M8-050", "STEP-M8-060"]
evidence:
  commits: ["c91df5e"]
  tests: ["verifies: REQ-TOOL-016 — tests/unit/test_ext_tool.c (4 cases: QR v1 matrix — 15 lines, all-light quiet rows, NULL/oversize refusal incl. the 80-col width fallback; pair happy/scan-timeout/406-refused with verbatim {kid,code} finalise pass-through and TWO login exchanges scripted (config + auth:ext:pair scopes); list EXTAID filter + [PROTECTED] + pid; login approved/rejected/timeout/NOAUTH exit-code matrix)"]
  notes: "qrcodegen v1.8.0 vendored VERBATIM (SHA-256 pinned in VENDORED.md) under src/tools/hem-tool/vendor/ — TOOL-ONLY linkage (hem-tool-core), export/header gates prove the SDK stays clean. ext_cmd.{h,c} in hem-tool-core: pair (checkin→login→config[eid/user/email/hostname]→session-POST-eid per the REQ-AUTH-008 binding finding→ext/init→register/init→INVERTED UTF-8 half-block QR w/ 4-module quiet zone→poll→validate→finalise; JSON-escaped payload composer), list (search_all EXTAID + keys.h protected classifier), login (config demo for system:config, confirm engine for custom scopes, optional NOAUTH pre-check). main.c: ext dispatch + --no-qr/--scope/--note/--timeout/--notify-url; --timeout also feeds ehem_options.confirm_timeout_ms. Unit 38/38 gcc+clang+ASan; gates green; MinGW cross-syntax clean (ext_cmd.c, qrcodegen.c). LIVE smoke: `ext list` → '0 paired authenticators' on the real device. Attended pair/login = M8-080."
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
- [x] Unit (hem-tool-core): pair leg order + verbatim finalise
      pass-through + exit codes; list filter + protected marks; login
      exit-code matrix (scripted broker)
- [x] Unit: QR matrix matches a qrcodegen reference fixture; width
      fallback exercised
- [x] `./dev ci` + asan green on gcc+clang; export/header gates green
      (qrcodegen tool-only); MinGW cross-syntax check clean
- [x] Tags placed; evidence filled
