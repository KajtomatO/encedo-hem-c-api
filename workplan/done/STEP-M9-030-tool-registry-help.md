---
id: STEP-M9-030
title: "Command registry: default URL + auth grouping + per-command help"
milestone: M9
implements: ["REQ-TOOL-017", "REQ-TOOL-019", "REQ-TOOL-020"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-020"]
evidence:
  commits: ["9e068cc", "3f50f12"]
  tests: ["tests/unit/test_registry.c (verifies: REQ-TOOL-017/019/020 — completeness walk, grouped top page shape, per-command help both depths, URL precedence + notice)"]
  notes: >
    NEW src/tools/hem-tool/registry.{h,c} in hem-tool-core: the command
    table (19 commands: name/family/synopsis/summary/details/auth-class/
    options), hem_help_top (38-line grouped page), hem_help_command
    (family summary at depth 1, command page at depth 2),
    hem_tool_resolve_url (flag > env > https://my.ence.do + one stderr
    notice). main.c: legacy 90-line usage() string DELETED — renders
    from the registry; -h/--help DEFERRED until the command is known
    (`keys rm --help` = command page); `help [<cmd> [<sub>]]` command
    added; URL resolves after parsing (o.url is flag-only now).
    ./dev ci 40/40 GCC+Clang + ASan clean; live: default-URL status
    reached my.ence.do with the notice, env-URL run notice-free.
    MinGW = CI-on-push.
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
- [x] Registry in hem-tool-core; unit test walks it and asserts every
      command carries auth class, synopsis, description, options
      (test_registry_complete).
- [x] Top-level help: three auth groups with meaningful headers, one
      line per command, no command-specific options, 38 lines
      (≤45 asserted).
- [x] Per-command help via both spellings at both depths (`help keys`
      family summary, `help keys rm` command-level); `--sigctx` shows
      in `help sign` and not in `help reboot`; `help nosuch` errors
      nonzero (unit + offline smoke).
- [x] URL resolution unit-tested for all three precedence levels;
      default use prints the stderr notice exactly once, stdout
      untouched (`--raw` pipelines clean).
- [x] Live (2026-08-06): `hem-tool status` with clean env reached the
      dev device via the default URL, notice printed.
- [x] Unit suite green GCC+Clang+ASan (40/40); export/header gates
      green; MinGW leg = CI-on-push.
