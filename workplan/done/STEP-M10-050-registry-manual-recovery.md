---
id: STEP-M10-050
title: "Registry: manual-recovery help section; settle the sub U/M question for config writes"
milestone: M10
implements: ["REQ-TOOL-019"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-TOOL-019 — tests/unit/test_registry.c test_manual_recovery_section (section/class data, listing order, cert-install + tls-recover only below the header, reboot still bearer, per-command wording) + tests/unit/test_tool_auth.c test_auth_class_guard (hem_tool_check_auth_class refuses --mobile for PASSPHRASE_ONLY commands with the sub=\"U\"/\"M\" reason; bearer/no-auth/unknown pass)"
  notes: >
    2026-10-09. registry.h: hem_cmd_section {MAIN, MANUAL_RECOVERY} +
    hem_command.section (positional tables now carry it explicitly —
    -Wmissing-field-initializers); registry.c: cert-install and tls-recover
    → HEM_AUTH_PASSPHRASE_ONLY + HEM_SECTION_MANUAL_RECOVERY with the reason
    in their details; print_group filters by section; hem_help_top renders
    the trailing "manual recovery (building blocks of `recovery`;
    passphrase)" section after the three auth groups. tool_auth.{h,c}:
    hem_tool_check_auth_class(cmd, mobile, err) — the generic guard; main.c
    looks the command up in the registry (bare name first: `sign KID`) and
    runs it before any traffic → `tls-recover --mobile` exits 2 with the
    message, `keys list --mobile` proceeds. The sub U/M question SETTLED by
    firmware read: api_post_system_config (api_system.c:1052-1066) demands
    scope system:config AND sub ∈ {U, M} → cert-install/tls-recover
    passphrase-only (rev 1 of TOOL-019 was wrong); api_get_system_reboot
    (:2174-2213) checks scope only → reboot stays mobile-capable. No live
    --mobile probe possible (device cert currently valid). Recorded in
    REQ-TOOL-019 (criteria resolved) and REQ-TOOL-018 rev 3; README hem-tool
    paragraph updated. Verified: ./dev ci 43/43 gcc+clang; ./dev test asan
    clean; ./dev check green; MinGW -fsyntax-only clean on registry.c,
    tool_auth.c, main.c; help page rendered and the guard exercised by
    hand (exit 2 + message). REQ-TOOL-019 → verified.
reopened: []
cancelled: null
---

**Goal:** REQ-TOOL-019 rev 2 realised: `hem_command` gains a section
field (data, not prose); `hem_help_top` renders the three auth groups
and then a trailing **"manual recovery"** section holding `cert-install`
and `tls-recover` (and nothing else); per-command help still shows each
one's auth class. `test_registry.c` extended (section membership,
rendering order, the completeness walk).

**Notes:** Second deliverable — the open criterion in REQ-TOOL-019:
firmware `api_system.c:1060-1066` demands token `sub` U or M on EVERY
`POST /api/system/config`, which a mobile bearer (`sub=base64(kid)`)
cannot satisfy. Settle it by firmware read (the source of record) plus,
if the device cert happens to be expired, one live `cert-install
--mobile` probe; also read the reboot handler's role check. If
confirmed: `cert-install` and `tls-recover` become PASSPHRASE_ONLY in
the registry, the tool_auth chokepoint rejects `--mobile` for them with
the ext-pair message shape, and REQ-TOOL-018 gets a rev recording the
correction (its "every bearer-needing subcommand" claim narrows). Record
the answer in REQ-TOOL-019 and REQ-TOOL-018. REQ-TOOL-019 returns to
`verified` through §4.3 once the new test passes.

**Definition of done**
- [x] Section field + rendering; `cert-install`/`tls-recover` appear only
      in the manual-recovery section; new M10 commands land in their
      auth groups as they arrive.
- [x] test_registry.c: section membership + order + completeness green.
- [x] sub U/M question answered and recorded (REQ-TOOL-019 criterion
      checked; REQ-TOOL-018 rev 3; registry auth classes match; the
      generic guard enforces them).
- [x] `./dev ci` + gates green; `implements: REQ-TOOL-019` tag kept.
