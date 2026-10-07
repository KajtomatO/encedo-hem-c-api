---
id: REQ-TOOL-023
title: hem-tool recovery — diagnose TLS/certificate trouble and run the matching remediation
status: approved
priority: should
revision: 1
source: user decision 2026-10-07 ("add to tool 'recovery' … based on previous problems"; single check-in attempt, no polling); incidents: expired device certificate (M1 gate 2026-07-16, REQ-NET-005; again 2026-10-06/07 — cloud renewed late, a cloud defect Encedo fixed after notification), TLS material lost after the 2026-07-22 wipe (REQ-SYS-013/TOOL-015), RTC unset after cold boot and ~8 % clock drift (KNOWN-ISSUES, REQ-AUTH-004/005); REQ-TOOL-003 and REQ-TOOL-015 (the building blocks); approved 2026-10-07 (M10 decomposition, user go-ahead)
depends_on: ["REQ-TOOL-003", "REQ-TOOL-015", "REQ-SYS-003", "REQ-NET-005", "REQ-TOOL-018"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool recovery — diagnose TLS/certificate trouble and run the matching remediation

`hem-tool recovery` SHALL probe the device, classify what is wrong with
its TLS/certificate state, and run exactly the remediation the recorded
incidents call for — in one command, with a single attempt per leg (no
polling for the cloud).

- **Probe:** `GET /api/system/status` under the context's normal TLS mode
  (system trust unless `--insecure`/`--cacert` was given).
- **Classification and action:**
  1. *Healthy* — status answers with `https: true` → run one check-in
     (sets the RTC after a cold boot, resyncs the drifting clock,
     REQ-SYS-003) and report "nothing to recover"; exit 0.
  2. *Expired certificate* — the TLS failure is the REQ-NET-005
     expired-cert case → one check-in; if it delivers a new chain
     (REQ-SYS-006), run the `cert-install` sequence (REQ-TOOL-003:
     install under `--insecure`, reboot, poll, verify under system
     trust); exit 0. If the check-in delivers **no** chain: report that
     the cloud has not issued a renewal, point at rerunning later, exit
     3 — **one attempt, no waiting** (user decision 2026-10-07: the
     2026-10-07 late renewal was a cloud defect since fixed).
  3. *HTTPS down* — the https probe cannot connect → probe the same host
     over **http://**; if it answers with `https: false` (TLS material
     lost, the post-wipe state), run the `tls-recover` sequence
     (REQ-TOOL-015: check-in, login, `ehem_tls_recover`, reboot, poll
     until `https: true`); exit 0.
  4. *Unreachable* — neither scheme answers → exit 4 ("device
     unreachable — power-cycle?", the KNOWN-ISSUES stall note).
  5. *Other TLS failure* (hostname mismatch, unknown CA, …) → report the
     SDK detail and the manual options; exit 5. The tool never widens
     trust on its own except for the install leg of case 2.
- **Exit codes:** 0 recovered or healthy; 1 login/device/cloud failure;
  2 usage/environment; 3 cloud has not renewed yet; 4 unreachable or
  bounded wait exhausted; 5 unclassified TLS failure.
- **Auth:** passphrase only — the install legs write `/api/system/config`,
  which the firmware allows for `sub` U/M only (REQ-SYS-014 finding);
  the probe and check-in need no credentials.
- **Help placement:** `recovery` is listed in its auth group;
  `cert-install` and `tls-recover` move to the "manual recovery" section
  (REQ-TOOL-019 rev 2). Orchestration lives in hem-tool-core and reuses
  the cert-install and tls-recover code paths rather than duplicating
  them.

**Rationale:** every TLS/certificate incident so far was solved by the
same three building blocks in the right order, discovered by hand each
time (a day for the 2026-07-22 recovery; ~1 h for the 2026-10-07
rotation). One command that picks the order removes the archaeology.

**Acceptance criteria:**
- [ ] Classification needs a public signal for "TLS failed because the
      certificate expired" — today that is internal
      (`ehem_transport_last_tls_expired`, src/transport.h). RESOLVE at
      implementation, user decision: (a) expose it on `ehem_last_error()`
      (a new field — library-allocated outputs may grow in 1.x,
      REQ-API-008) or (b) classify from the REQ-NET-005 detail text the
      SDK already emits.
- [ ] Unit (hem-tool-core, fake transport): one test per case 1–5 —
      healthy (status + one check-in, exit 0); expired + chain → the
      full cert-install sequence, exit 0; expired + no chain → exit 3
      with NO retry; https down + http `https:false` → the tls-recover
      sequence, exit 0; both down → exit 4; other TLS failure → exit 5;
      `--mobile` → exit 2.
- [ ] Live, attended (non-destructive on a healthy device): `recovery`
      exits 0 via case 1 with one check-in. The other cases are
      exercised whenever the incident recurs (recorded here with date).
- [ ] `--help` explains the five cases and that the cloud is tried once.
