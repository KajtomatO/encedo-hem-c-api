---
id: STEP-M10-060
title: "hem-tool recovery: probe, classify, run cert-install / tls-recover / check-in (single cloud attempt)"
milestone: M10
implements: ["REQ-TOOL-023"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M10-050"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Command + registry entry + per-command help.
- [ ] Unit (tests/unit/test_recovery.c, fake transport): one test per
      case incl. "expired + no chain → exit 3 with no second check-in";
      `--mobile` → exit 2.
- [ ] Expired-cert signal decision recorded in REQ-TOOL-023 (and
      REQ-API-004 if (a)).
- [ ] Live, attended, non-destructive: `recovery` on the healthy dev
      device → case 1, exit 0 (recorded in REQ-TOOL-023).
- [ ] `./dev ci` + gates green; `implements: REQ-TOOL-023` tagged.
