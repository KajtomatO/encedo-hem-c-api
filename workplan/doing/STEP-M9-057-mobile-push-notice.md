---
id: STEP-M9-057
title: "Mobile push notices: announce every push's scope"
milestone: M9
implements: ["REQ-AUTH-010", "REQ-TOOL-018"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M9-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** A `--mobile` command is never silent about a push. User request
at gate prep (2026-08-06): "each time hem-tool asks the mobile app it
should print what it asks for." New append-only `ehem_options` hook
`confirm_notice(scope, timeout_ms, arg)` — the confirm engine calls it
once per DELIVERED push (after the broker accepted the confirmation
request, before the wait begins). hem-tool sets it under `--mobile` and
prints `mobile: push sent — approve "<scope>" on your phone (waiting up
to N s)` to stderr — one line per scope acquisition, so multi-scope
commands (e.g. keys rm: list + del) explain each push.

**Notes:** The library never prints (hem-tool is the UI) — hence a hook,
not SDK output. Options growth is legal post-freeze by design (the
abi_size discipline; no new exported symbols, export baseline
untouched). ext login keeps its own richer push line; the hook is set
only under `--mobile`, so no double-printing there.

**Definition of done**
- [ ] `ehem_options.confirm_notice`/`confirm_notice_arg` (append-only,
      EHEM_OPT_HAS-guarded copy); fired once per delivered push incl.
      after the drift-recovery re-fire; NULL default = off.
- [ ] Unit (test_confirm.c): hook receives the requested scope + the
      effective timeout, exactly once per begin (incl. the recovery
      path); silent when unset.
- [ ] hem-tool prints the stderr notice under `--mobile`; usage/help
      unchanged (M9-030 registry already documents --mobile).
- [ ] REQ-AUTH-010 + REQ-TOOL-018 rev bumps record the hook/notice;
      API-GUIDE auth section mentions it.
- [ ] `./dev ci` + ASan green; live: user sees the notice on the next
      --mobile run.
