---
id: REQ-TOOL-020
title: hem-tool per-command help
status: needs-reverify
priority: should
revision: 2   # 2026-10-19 (STEP-M12-135, §6.2 approved in chat): `--passphrase` is deprecated (user decision 2026-10-19 after the rc1 test — a passphrase on the command line stays in the shell history and shows in the process list; the flag will be removed in a later release, `EHEM_PASSPHRASE` stays): the shared options render it DEPRECATED and hem-tool warns once on stderr when the flag is used; a criterion added; needs-reverify until test_registry and tool_startup_cli are green again
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
    `--passphrase`, `--mobile`). **`--passphrase` is deprecated** (rev 2,
    user decision 2026-10-19): the shared options render it DEPRECATED,
    pointing at `EHEM_PASSPHRASE`, because a passphrase on the command
    line stays in the shell history and shows in the process list; the
    flag will be removed in a later release (a safer entry is a BACKLOG
    item), and hem-tool prints one `warning:` line on stderr whenever the
    flag is used, before anything else.
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
- [ ] Unit (rev 2): the top page renders `--passphrase PW` as DEPRECATED
      pointing at `EHEM_PASSPHRASE` (test_registry; per-command pages keep
      pointing at the shared options); on the real `main.c`
      (`tool_startup_cli`) `--passphrase x keys types` and the `=`
      spelling print exactly one `warning:` line on stderr and still run
      (exit 0); without the flag stderr stays empty.
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
