---
id: REQ-TOOL-022
title: hem-tool wipe-device — factory reset with an unbypassable confirmation
status: draft
priority: should
revision: 1
source: user decision 2026-10-07 (M10: "add both init-device and wipe-device to the tool"; attended-only); REQ-SYS-014 (the binding); ARCHITECTURE.md §8 protected-key confirmation convention
depends_on: ["REQ-SYS-014", "REQ-TOOL-018", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool wipe-device — factory reset with an unbypassable confirmation

`hem-tool wipe-device [--wait]` SHALL factory-reset the device through
`ehem_system_wipeout` (REQ-SYS-014) only after an interactive
confirmation that `--yes` does not satisfy: the tool prints the device's
identity (hostname, `devid`, `instanceid` from `ehem_system_config`) and
proceeds only when the operator types that hostname exactly.

- **Auth:** passphrase only — the firmware demands token `sub` U or M
  for config writes (REQ-SYS-014), so `--mobile` is rejected up front
  with the same message shape as `ext pair --mobile` (REQ-TOOL-018).
- **Flow:** login → `ehem_system_config` (identity shown) →
  confirmation → `ehem_system_wipeout` → report "wipe accepted; the
  device erases its configuration and restarts in ~2 s". With `--wait`:
  poll `GET /api/system/status` over the device's **http://** URL
  (HTTPS is gone with the TLS material) until it answers, bounded
  (~180 s, reboot-style pacing), then print that the device is
  uninitialised and point at `init-device` and `recovery`.
- **Exit codes** (the reboot/tls-recover convention): 0 = wipe accepted
  (and, with `--wait`, the device came back); 1 = login/device failure;
  2 = usage/environment; 3 = confirmation declined; 4 = `--wait`
  timeout.
- DISRUPTIVE and irreversible; `--help` says so and names what is lost
  (keys, user passwords, TLS material, logs).

**Rationale:** the wipe is the most destructive call in the API and the
precondition for re-initialisation. The typed-hostname confirmation
reuses the protected-key idea (explicit, per-target, `--yes` ignored)
with a stronger token than `YES`, because the target is the whole
device.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, fake transport): declined confirmation → exit
      3 with zero device writes; wrong hostname → exit 3; correct
      hostname → the wipeout request is sent exactly once; `--yes` does
      not skip the prompt; `--mobile` → exit 2 naming the sub="U"
      constraint; `--wait` poll until status answers / exit 4 on
      exhaustion.
- [ ] **Attended-only** (REQ-TEST-007): no live CTest. Evidence to record
      here: date, device, typed confirmation, observed restart, device
      back uninitialised.
- [ ] `--help` and the per-command help (REQ-TOOL-020) document the
      confirmation, the irreversibility and the follow-up commands.
