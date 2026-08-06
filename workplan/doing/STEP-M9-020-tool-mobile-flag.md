---
id: STEP-M9-020
title: "hem-tool --mobile via a shared login helper"
milestone: M9
implements: ["REQ-TOOL-018"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** A shared login helper in hem-tool-core replaces the 13
scattered `ehem_login(ctx, o->passphrase)` call sites; a global
`--mobile` flag routes every bearer-needing subcommand through
`ehem_login_mobile` instead, with `--timeout` mapping to
`confirm_timeout_ms` and distinct exits for rejected / timeout /
nothing-paired on every subcommand.

**Notes:** Precedence per REQ-TOOL-018: `--mobile` overrides env
`EHEM_PASSPHRASE` (hem.env is commonly auto-sourced); `--mobile` +
explicit `--passphrase` = usage error; `ext pair` rejects `--mobile`
naming the sub="U" device constraint; no-auth commands ignore it like
they ignore `--passphrase`. Unit tests use the scripted-broker/fake
transport pattern from test_ext_tool.c. The ATTENDED live leg (real
phone approve + reject on a representative command) is deliberately
deferred to the M9-060 gate — broker pushes are never sent unattended
(REQ-TEST-006 policy) — so REQ-TOOL-018 stays `implemented` until then.

**Definition of done**
- [ ] Shared helper in hem-tool-core; grep shows no direct
      `ehem_login(` call sites left outside it (and `ext login`'s
      existing `ehem_login_mobile` path routes through or beside it
      coherently).
- [ ] All bearer-needing subcommands accept `--mobile` (keys *, sign,
      random, logs *, selftest, cert-install, tls-recover, reboot,
      ext list).
- [ ] Unit: passphrase-vs-mobile routing; `--mobile --passphrase` →
      usage error; `ext pair --mobile` → sub="U" error; approve /
      reject / timeout / nothing-paired exits on a representative
      command — all green on GCC+Clang+ASan.
- [ ] Export/header gates green; MinGW cross-syntax check clean.
- [ ] Help/usage text documents `--mobile` (full restructure lands in
      M9-030).
