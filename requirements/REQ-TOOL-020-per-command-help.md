---
id: REQ-TOOL-020
title: hem-tool per-command help
status: verified
priority: should
revision: 1
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11)
depends_on: ["REQ-TOOL-019"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool per-command help

hem-tool SHALL provide per-command help — `hem-tool <command> --help`
and `hem-tool help <command>` — showing that command's synopsis,
description, auth requirement, and only the options that apply to it.

- **Today's problem:** one flat `usage()` dump — 24 global option lines
  where `--sigctx` (sign-only) sits next to `--wait` (reboot-only) —
  disconnected from the 20 commands they belong to.
- **Structure:**
  - `hem-tool <command> --help` / `hem-tool help <command>`: synopsis
    (positional args spelled out), one-paragraph description, auth
    requirement, then *only* that command's options plus the shared
    connection/auth options (`--url`, `--cacert`, `--insecure`,
    `--passphrase`, `--mobile`).
  - Top-level `hem-tool --help` / bare `hem-tool`: compact summary —
    the REQ-TOOL-019 auth-grouped command list (one line per command)
    plus the shared connection/auth options; no per-command option
    dump; a closing pointer to `hem-tool help <command>`.
  - `hem-tool help` with no argument = top-level help;
    `hem-tool help <unknown>` = error naming the unknown command,
    exit nonzero.
- Synopses, descriptions, auth classes, and option lists all come from
  the shared command registry in hem-tool-core (single source of truth
  with REQ-TOOL-019) — main.c only renders.
- Subcommand families (`keys`, `logs`, `ext`) resolve at both depths:
  `help keys` summarizes the family, `help keys rm` (or
  `keys rm --help`) is command-level.

**Rationale:** the flat options list stopped scaling milestones ago;
per-command help is the difference between a demo tool and a 1.0 tool,
and the tool doubles as the SDK's living documentation (ARCHITECTURE
§8) — help quality is documentation quality. User decision 2026-08-06.

**Acceptance criteria:**
- [x] Every registered command (19 commands, both family depths)
      yields per-command help via both spellings (`<cmd> --help`
      deferred-parse + `help <cmd>`); test_registry_complete walks the
      registry asserting non-empty synopsis/summary/details
      (test_registry.c + offline smoke, 2026-08-06).
- [x] Unit: `--sigctx` appears in `help sign` and NOT in
      `help reboot`; both point at the shared connection/auth options
      (test_registry.c test_per_command_help, 2026-08-06).
- [x] Top-level help contains no command-specific option lines
      (--sigctx/--wait/--label-prefix asserted absent) and renders at
      38 lines (≤45 asserted; test_registry.c test_top_page_shape).
- [x] `hem-tool help nosuch` errors naming the unknown command, exit
      2 (hem_help_command → -1 unit-tested; main.c smoke 2026-08-06).
