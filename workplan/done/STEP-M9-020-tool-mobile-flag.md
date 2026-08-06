---
id: STEP-M9-020
title: "hem-tool --mobile via a shared login helper"
milestone: M9
implements: ["REQ-TOOL-018"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: ["c28eee8"]
  tests: ["tests/unit/test_tool_auth.c (verifies: REQ-TOOL-018 — routing, exit mapper 13/14 + hint, keys list --mobile approve/reject/timeout vs scripted broker, ext pair --mobile rejection)"]
  notes: >
    ./dev ci green (GCC+Clang, 39 CTest units incl. new test_tool_auth;
    export/header gates) + ./dev test asan clean. Offline smoke:
    --mobile --passphrase → exit 2; ext pair --mobile → exit 2 +
    sub="U"; credential-less keys list → exit 2 naming --mobile.
    LIVE (my.ence.do): passphrase-mode keys list green — NO live
    --mobile run (broker pushes never unattended, REQ-TEST-006);
    the attended approve/reject demo is the REQ's open criterion,
    scheduled at the M9-060 gate. Design decisions this step (user,
    2026-08-06): nothing-paired = timeout+hint (REQ-TOOL-018 rev 2);
    tool-wide mobile exits 13/14 (ext login keeps 3/4/5). recover.c
    keeps its fail-fast credential check BEFORE any probe (zero-traffic
    semantics, caught by test_recover). MinGW = CI-on-push.
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
- [x] Shared helper (`tool_auth.{h,c}`) in hem-tool-core; grep shows
      ZERO `ehem_login(` call sites outside it — ext pair and ext
      login's pre-check route through it with mobile=false; ext login
      keeps its direct `ehem_login_mobile` (inherently mobile demo).
- [x] All bearer-needing subcommands accept `--mobile` (keys
      list/pub/gen/rm/update, sign, random, logs list/get/key,
      selftest, cert-install, tls-recover, reboot, ext list).
- [x] Unit (test_tool_auth.c): passphrase-vs-mobile routing (both
      lazy); missing-credential ARG; `ext pair --mobile` → sub="U"
      error, zero traffic; approve / reject(14) / timeout(13 + `ext
      list` hint) on `keys list --mobile` vs a scripted broker; mobile
      outranks an env-style passphrase — green GCC+Clang+ASan. The
      `--mobile --passphrase` conflict lives in main.c arg parsing
      (exe-only) — proven by offline smoke (exit 2), not unit.
      Nothing-paired = timeout+hint (user decision 2026-08-06,
      REQ-TOOL-018 rev 2) — the original "distinct nothing-paired
      exit" wording was reworked.
- [x] Export/header gates green (in `./dev ci`); MinGW leg =
      CI-on-push (no new format strings; identifiers checked).
- [x] Help/usage text documents `--mobile` incl. exits 13/14 (full
      restructure lands in M9-030).
