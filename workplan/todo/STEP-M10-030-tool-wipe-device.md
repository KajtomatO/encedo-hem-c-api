---
id: STEP-M10-030
title: "hem-tool wipe-device: typed-hostname confirmation, --wait over http"
milestone: M10
implements: ["REQ-TOOL-022"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M10-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `hem-tool wipe-device [--wait]` in hem-tool-core (new
`wipe.c/h`, registry entry, auth class PASSPHRASE_ONLY): login →
`ehem_system_config` (print hostname / devid / instanceid) → prompt that
requires the exact hostname (`--yes` ignored; non-TTY stdin reads the
line the same way so the unit test can drive it) → `ehem_system_wipeout`
→ "wipe accepted" message; `--wait` polls `GET /api/system/status` over
the http:// form of the URL (bounded ~180 s, reboot-style pacing) and
reports "device back, uninitialised — next: init-device, recovery".
Exit codes 0/1/2/3 (declined)/4 (wait timeout).

**Notes:** Reuse the protected-key prompt plumbing (keys.c `YES` prompt)
and recover.c's status polling. `--mobile` → exit 2 naming the sub="U"/M
device constraint (tool_auth chokepoint, like ext pair). `--help` names
what is lost (keys, user passwords, TLS material, logs, the phone
pairing). Attended-only (REQ-TEST-007): no live test; the attended wipe
is the first act of STEP-M10-070.

**Definition of done**
- [ ] Command + registry entry + per-command help (REQ-TOOL-020 gate:
      test_registry completeness walk passes).
- [ ] Unit (tests/unit/test_wipe.c, fake transport): declined / wrong
      hostname → exit 3, zero writes; correct hostname → one wipeout
      request; `--yes` does not skip the prompt; `--mobile` → exit 2;
      `--wait` success and exhaustion (exit 4).
- [ ] No live test added; `./dev ci` + gates green; `implements:
      REQ-TOOL-022` tagged.
