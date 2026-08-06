---
id: REQ-TOOL-018
title: hem-tool --mobile — mobile auth for every bearer-needing subcommand
status: approved
priority: should
revision: 1
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11; user's working name "--app-auth", finalized "--mobile" to match ehem_login_mobile and the start_point HEM-CFG-3 vocabulary `auth = passphrase | mobile`)
depends_on: ["REQ-AUTH-006", "REQ-AUTH-010", "REQ-TOOL-016"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool --mobile — mobile auth for every bearer-needing subcommand

Every hem-tool subcommand that needs a bearer token SHALL accept a
global `--mobile` flag selecting mobile push confirmation
(`ehem_login_mobile`) instead of passphrase login.

- **One login chokepoint:** a shared helper in hem-tool-core replaces
  the 13 scattered `ehem_login(ctx, o->passphrase)` call sites; every
  subcommand routes through it, so `--mobile` (and any future auth
  mode) lands in exactly one place.
- **Precedence:** `--mobile` overrides an `EHEM_PASSPHRASE` from the
  environment (hem.env is commonly auto-sourced by `./dev`); passing
  `--mobile` together with an explicit `--passphrase` flag is a usage
  error.
- **Passphrase-only commands:** `ext pair` rejects `--mobile` with a
  message naming the reason — the device demands `sub="U"` for the
  pairing trio (REQ-AUTH-006), and mobile bearers carry
  `sub=base64(kid)` (REQ-AUTH-007). No-auth commands (`status`,
  `checkin`) ignore the flag like they ignore `--passphrase` today.
- **Confirmation semantics** mirror `ext login` (REQ-TOOL-016):
  `--timeout SEC` maps to `ehem_options.confirm_timeout_ms` (default
  60 s); rejected-on-phone, timeout, and nothing-paired each produce a
  distinct nonzero exit and message, on every subcommand.
- Scope behavior is the SDK's own (REQ-AUTH-010): one push per scope
  needing a bearer — a multi-scope command (e.g. `cert-install`) may
  push more than once; the tool documents this in its help.

**Rationale:** with a phone paired, no command should force the
passphrase onto a command line — this is HEM-CFG-3's
`auth = passphrase | mobile` made real in the reference consumer, and
the 1.0 polish item the user requested 2026-08-06.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, scripted broker/fake transport per the
      test_ext_tool.c pattern): the shared helper routes passphrase vs
      mobile; `--mobile` + `--passphrase` → usage error;
      `ext pair --mobile` → the sub="U" error; approve / reject /
      timeout / nothing-paired exits proven on at least one
      representative subcommand.
- [ ] All bearer-needing subcommands (keys *, sign, random, logs *,
      selftest, cert-install, tls-recover, reboot, ext list) accept
      `--mobile`; grep shows no remaining direct
      `ehem_login(ctx, o->passphrase)` call sites outside the helper.
- [ ] Live (ATTENDED — broker pushes are never sent unattended, the
      REQ-TEST-006 policy): one representative command (e.g.
      `keys list --mobile`) approved on the real phone; a second run
      rejected → distinct exit. OPEN until the attended run.
