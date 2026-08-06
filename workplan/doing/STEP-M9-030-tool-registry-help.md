---
id: STEP-M9-030
title: "Command registry: default URL + auth grouping + per-command help"
milestone: M9
implements: ["REQ-TOOL-017", "REQ-TOOL-019", "REQ-TOOL-020"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** hem-tool grows a command registry in hem-tool-core — per
command: name, family, synopsis, description, auth class, option list —
and main.c only renders it. From the registry: the top-level listing
grouped by auth requirement (none / any bearer / passphrase-only,
REQ-TOOL-019), per-command help via `hem-tool <cmd> --help` and
`hem-tool help <cmd>` at both family depths (REQ-TOOL-020). URL
resolution moves into hem-tool-core with the new default:
`--url` > `EHEM_URL` > `https://my.ence.do`, one stderr notice when the
default is used (REQ-TOOL-017).

**Notes:** Depends on M9-020 so `--mobile` exists before help is
restructured (auth classes name it; per-command option lists include
it). The registry is the single source of truth REQ-TOOL-019/020 both
demand — a command without an auth class or synopsis fails the
completeness unit test, so the listing can't drift from the code.
Top-level help shrinks to a one-screen grouped summary; the 24-line
flat options dump is deleted, options render only under their commands
(shared connection/auth options excepted).

**Definition of done**
- [ ] Registry in hem-tool-core; unit test walks it and asserts every
      command carries auth class, synopsis, description, options.
- [ ] Top-level help: three auth groups with meaningful headers, one
      line per command, no command-specific options, fits ~one screen.
- [ ] Per-command help via both spellings at both depths (`help keys`
      family summary, `help keys rm` command-level); `--sigctx` shows
      in `help sign` and not in `help reboot`; `help nosuch` errors
      nonzero.
- [ ] URL resolution unit-tested for all three precedence levels;
      default use prints the stderr notice exactly once, stdout
      untouched (`--raw` pipelines clean).
- [ ] Live: `hem-tool status` with clean env (no --url/EHEM_URL)
      reaches the dev device via the default URL.
- [ ] Unit suite green GCC+Clang+ASan; export/header gates green;
      MinGW cross-syntax check clean.
