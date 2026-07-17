---
id: STEP-M7-070
title: "hem-tool keys update / logs / selftest subcommands"
milestone: M7
implements: ["REQ-TOOL-011", "REQ-TOOL-012", "REQ-TOOL-013"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M7-020", "STEP-M7-040", "STEP-M7-050"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Subcommands wired in hem-tool + hem-tool-core, tagged
      `implements: REQ-TOOL-011` / `REQ-TOOL-012` / `REQ-TOOL-013`
- [ ] Unit tests green (gcc+clang+asan): guard prompt paths (`YES`,
      refusal, `--yes` ignored), rename-to-protected warning, logs
      pagination walk + file output bytes, selftest exit codes 0/1/3,
      usage errors exit 2
- [ ] Live demo: EHEMTEST rename + descr set + cleanup; logs list/get/
      key (or EPA message); selftest exit 0 — all three demonstrated
      and recorded
- [ ] README + `--help` updated; shellcheck-clean where touched;
      export/header gates green; MinGW cross-syntax check run
