---
id: STEP-M7-070
title: "hem-tool keys update / logs / selftest subcommands"
milestone: M7
implements: ["REQ-TOOL-011", "REQ-TOOL-012", "REQ-TOOL-013"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M7-020", "STEP-M7-040", "STEP-M7-050"]
evidence:
  commits: ["b954563"]
  tests: ["verifies: REQ-TOOL-011/012/013 (tests/unit/test_tool_m7.c — 8 cases, hem-tool-core via fake transport); live CLI demo green 2026-07-18"]
  notes: >
    keys update in keys.{h,c}; NEW logs.{h,c} + selftest.{h,c} in
    hem-tool-core; main.c dispatch + usage + --out flag. Unit 31/31
    gcc+clang + ASan; export/header gates green; MinGW cross-syntax OK on
    all touched TUs. DESIGN (REQ-TOOL-011 rev2, from the REQ-KEY-007
    whole-record finding): --descr omitted → the tool RE-SENDS the stored
    descr (preserve, with an stderr note); --descr "" clears. Protected
    guard reuses hem_key_is_protected + the keys-rm literal-YES ritual
    (--yes ignored); rename-to-protected warns. selftest exit 3 =
    device-reported fail state (distinct from unreachable). LIVE DEMO
    (my.ence.do): gen → keys update (rename+descr; second rename fired
    the preserve note "7 bytes") → list showed it → selftest PASS exit 0
    (UTC timestamps, se_state 0, repo stats) → logs list 62 ids →
    logs key real Ed25519 triple → logs get pipe-delimited file →
    keys rm cleanup (1 deleted). No shell files changed (shellcheck n/a).
reopened: []
cancelled: null
---

**Goal:** three new hem-tool subcommands over the public API only:
`keys update` (protected-key guard), `logs list|get|key`, and
`selftest` (exit code carries the health verdict).

**Notes:** Orchestration into hem-tool-core (cert_install.c precedent)
so the CLI, unit tests, and live demos share one implementation. keys
update must `get`/classify the CURRENT label before mutating (REQ-TOOL-005
classifier reuse — it already lives in hem-tool-core); prompt ritual and
`--yes`-ignored semantics copied from keys rm, plus the warn-on-rename-
to-protected stderr path. logs get: binary-safe stdout/`--out` with the
`_setmode` discipline from sign; logs key prints base64 triple only (no
local verify — shim is not public API). selftest exit codes 0/1/3
(3 = device-reported fail state). EPA fallback messages per
REQ-TOOL-012. README §tool table + `--help` for all three.

**Definition of done**
- [x] Subcommands wired in hem-tool + hem-tool-core, tagged
      `implements: REQ-TOOL-011` / `REQ-TOOL-012` / `REQ-TOOL-013`
- [x] Unit tests green (gcc+clang+asan): guard prompt paths (`YES`,
      refusal, `--yes` ignored), rename-to-protected warning, logs
      pagination walk + file output bytes, selftest exit codes 0/1/3,
      usage errors exit 2
- [x] Live demo: EHEMTEST rename + descr set + preserve-note + cleanup;
      logs list/get/key (PPA path); selftest exit 0 — all three
      demonstrated and recorded
- [x] `--help` updated (README has no per-subcommand table by design);
      no shell files touched; export/header gates green; MinGW
      cross-syntax check run
