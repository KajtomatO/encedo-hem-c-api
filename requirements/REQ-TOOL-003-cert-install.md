---
id: REQ-TOOL-003
title: hem-tool cert-install — harvest, install, reboot, verify
status: approved
priority: must
revision: 1
source: user decision 2026-07-16 ("implement the working cert install" → "plan hem-tool cert-install"); live remediation procedure executed 2026-07-16 (python script); REQ-SYS-003 root-cause finding
depends_on: ["REQ-TOOL-001", "REQ-SYS-004", "REQ-SYS-005", "REQ-SYS-006", "REQ-AUTH-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool cert-install — harvest, install, reboot, verify

hem-tool SHALL provide a `cert-install` subcommand automating the manual
remediation proven on 2026-07-16, for devices whose firmware (v1.2.2)
cannot apply check-in certificate updates itself:

1. **Harvest:** run the check-in flow; take the cloud-delivered chain
   (REQ-SYS-006). No chain → explain and exit nonzero.
2. **Skip-if-current:** if the certificate the device currently serves
   already matches the harvested chain's leaf, report "already current"
   and exit 0 without touching the device; `--force` overrides.
3. **Install:** authenticate (passphrase from `EHEM_PASSPHRASE` or
   `--passphrase`, consistent with existing tool conventions) and install
   via REQ-SYS-004; require `reboot_required` in the reply.
4. **Reboot:** REQ-SYS-005, then poll until the device answers again
   (bounded, ~2 min), tolerating the connection-refused window.
5. **Verify:** re-fetch device status under the context's normal TLS mode
   and report the outcome, including the new chain's leaf validity dates.
   In SYSTEM trust mode a successful verify proves the rotation end to
   end; with `--insecure` the tool says verification was skipped.

The command mutates device state and reboots it: it SHALL print what it is
about to do, and its live integration test is disruptive-gated
(`disruptive` CTest label + `EHEM_ALLOW_DISRUPTIVE=1`) — the unit-level
flow is covered via the fake transport.

**Acceptance criteria:**
- [ ] Full flow against the fake transport: check-in legs, install POST
      body, reboot GET, polling, and final verify asserted in sequence;
      exit 0 with a summary naming old→new validity (unit test).
- [ ] Skip-if-current path exits 0 without install/reboot; `--force`
      proceeds (unit test). OPEN: mechanism for reading the served
      certificate's identity for the comparison — candidate: leg-1
      challenge `csn` claim (serial of the loaded cert, per
      system/checkin.md) vs the harvested leaf's serial; decide at
      implementation and record here.
- [ ] Each failure mode (no chain, auth failure, install rejected, device
      never returns after reboot, verify fails) exits nonzero with a
      distinct, actionable message (unit tests).
- [ ] Live disruptive-gated integration run against the dev device recorded
      in evidence (may be deferred to the next natural expiry to avoid a
      pointless reboot; the 2026-07-16 python-script run is the reference
      behavior).
