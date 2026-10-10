---
id: STEP-M10-060
title: "hem-tool recovery: probe, classify, run cert-install / tls-recover / check-in (single cloud attempt)"
milestone: M10
implements: ["REQ-TOOL-023"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M10-050"]
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-TOOL-023 — tests/unit/test_recovery.c (healthy → one check-in, exit 0; expired + chain → one relaxed check-in, cert-install sequence, trusted verify, exit 0; expired + no chain → exit 3 after exactly one check-in; https down + http https:false → tls-recover sequence, exit 0; both down → exit 4; other TLS failure with https reported up OR not reported → exit 5, nothing written; --mobile / no passphrase → exit 2, zero traffic); REQ-API-004 rev 2 (ehem_error.tls_expired set on the expired probe, clear otherwise)"
    - "LIVE 2026-10-09 (attended, non-destructive): `./dev tool recovery` against my.ence.do under system trust → case 1: 'healthy: the device answers under the configured trust' → check-in → 'nothing to recover', exit 0; `./dev tool status` OK afterwards"
  notes: >
    2026-10-09. SDK: ehem_error.tls_expired appended (include/ehem/ehem.h;
    context.c resets it in clear/fail; proto_common.c sets it on the three
    TLS-failure exits from the transport's REQ-NET-005 verdict — also with
    automatic recovery off) — the public expired-cert signal (REQ-TOOL-023
    open criterion resolved as option (a); REQ-API-004 rev 2, mini §6.2:
    context-owned struct, append-only, ABI-safe; API-GUIDE error-model
    paragraph). Tool: hem-tool-core recovery.{h,c} (implements:
    REQ-TOOL-023) takes three caller-built contexts — configured trust with
    no_auto_checkin (the probe), insecure (the expired-cert install leg,
    delegating to hem_cert_install_run), http:// (the HTTPS-down leg,
    delegating to hem_tls_recover_run with force) — and the five-case
    decision tree; the cloud is asked ONCE (no renewal → exit 3). A status
    that omits the `https` field (the healthy dev device does, found live)
    is treated as UNKNOWN → exit 5, never a guessed tls-recover. main.c:
    fill_opts() factored out of make_ctx(); cmd_recovery builds the three
    contexts; registry entry PASSPHRASE_ONLY (main section); README.
    Verified: ./dev ci 46/46 gcc+clang (45 + test_recovery); ./dev test asan
    clean; ./dev check green; MinGW -fsyntax-only clean on recovery.c,
    main.c, context.c, proto_common.c; live case 1 on the dev device exit 0.
    REQ-TOOL-023 → verified (the other cases are exercised when the
    incident recurs — recorded in the REQ as the standing note).
reopened: []
cancelled: null
---

**Goal:** `hem-tool recovery` in hem-tool-core (new `recovery.c/h`,
registry entry in its auth group): status probe under the context's TLS
mode → five cases per REQ-TOOL-023 — healthy (one check-in, exit 0);
expired cert (one check-in; chain → the cert-install sequence under
`--insecure`, reboot, poll, verify under system trust, exit 0; no chain →
exit 3, **no retry**); HTTPS down + http `https:false` → the tls-recover
sequence, exit 0; unreachable → exit 4; other TLS failure → exit 5.
Reuses `cert_install.c` and `recover.c` code paths (refactor them into
callable sequences rather than copying).

**Notes:** **Resolve at step start (REQ-TOOL-023 open criterion):** the
public "certificate expired" signal — (a) a new field on
`ehem_last_error()` (append-only growth is allowed in 1.x, REQ-API-008;
mini §6.2 for REQ-API-004) or (b) classify from the REQ-NET-005 detail
text the SDK already emits (fragile; pin the substring in a test). The
user ruled out polling the cloud (2026-10-07): one check-in, then
report. Auth: passphrase-only until STEP-M10-050's finding says
otherwise. `--help` explains the five cases.

**Definition of done**
- [x] Command + registry entry + per-command help.
- [x] Unit (tests/unit/test_recovery.c, three fake transports): one test
      per case incl. "expired + no chain → exit 3 with no second
      check-in"; `--mobile` → exit 2.
- [x] Expired-cert signal decision recorded in REQ-TOOL-023 and
      REQ-API-004 rev 2: option (a) — `ehem_error.tls_expired` appended
      (context-owned struct, ABI-safe), set by the shared request path
      from the transport's REQ-NET-005 verdict.
- [x] Live, attended, non-destructive: `recovery` on the healthy dev
      device → case 1, exit 0 (recorded in REQ-TOOL-023; 2026-10-09).
- [x] `./dev ci` + gates green; `implements: REQ-TOOL-023` tagged.
