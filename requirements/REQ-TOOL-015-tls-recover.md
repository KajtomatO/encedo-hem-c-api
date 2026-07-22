---
id: REQ-TOOL-015
title: hem-tool tls-recover — one-command HTTPS restoration after TLS loss
status: approved
priority: should
revision: 1
source: user decision 2026-07-22; REQ-SYS-013 (the binding); the 2026-07-22 manual recovery sequence
depends_on: ["REQ-SYS-013", "REQ-SYS-001", "REQ-SYS-003", "REQ-SYS-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool tls-recover — one-command HTTPS restoration after TLS loss

`hem-tool tls-recover [--force]` SHALL restore the device's HTTPS end to
end: skip-if-healthy check, clock sync, cloud key+cert recovery
(REQ-SYS-013), reboot, and verification that the device serves HTTPS
again.

- **Intended use:** after a device wipe, against the device's HTTP URL
  (`hem-tool --url http://<host> tls-recover`) — the state in which the
  2026-07-22 recovery was needed. The full sequence:
  1. unauthenticated status GET — when the device already reports
     `https: true`, print "nothing to recover (renewals: cert-install)"
     and exit 0, unless `--force`;
  2. check-in (sets the RTC — required after a cold boot, when login
     would otherwise fail — and may refresh an expired cert on the way);
     a check-in failure warns and continues (the RTC may already be
     set);
  3. login + `ehem_tls_recover` (factory register endpoint);
  4. when the install reports `reboot_required`: reboot and poll status
     (bounded, cert-install-style pacing) until the device answers with
     `https: true`;
  5. report the restored state.
- Exit codes (the cert-install multi-code convention): 0 = recovered or
  nothing to do; 1 = login/device/cloud failure; 2 = usage/environment;
  3 = the cloud delivered no usable bundle; 4 = device did not return
  with HTTPS within the bounded wait.
- DISRUPTIVE (installs key material and reboots); `--help` says so.
  Orchestration lives in hem-tool-core so the CLI, unit tests, and any
  live demo drive one code path, public API only.

**Rationale:** the manual recovery took a day of discovery (browser tool
archaeology, firmware reading, scheme pitfalls — the recovery tool
itself defaults to `https://`, the very protocol that is down). One
command with a skip-guard makes the next wipe a non-event.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, fake transport): healthy-status skip (exit 0,
      no further traffic, `--force` overrides); the full downed-device
      sequence (status → check-in legs → login → recovery legs → reboot
      → poll until `https: true`) with exit 0; no-bundle → exit 3;
      poll exhaustion → exit 4; missing passphrase → exit 2.
- [ ] Live demo: on the healthy device `tls-recover` exits 0 via the
      skip path; `tls-recover --force` performs the full recovery
      (reinstall + reboot) and ends with the device serving HTTPS.
- [ ] `--help` documents the command, `--force`, and its disruptive
      nature.
