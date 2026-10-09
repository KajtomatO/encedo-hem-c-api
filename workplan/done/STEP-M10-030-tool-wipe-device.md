---
id: STEP-M10-030
title: "hem-tool wipe-device: typed-hostname confirmation, --wait over http"
milestone: M10
implements: ["REQ-TOOL-022"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M10-010"]
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-TOOL-022 — tests/unit/test_wipe.c (declined / wrong hostname / EOF → exit 3 with zero writes and the identity shown; exact hostname → exactly one {\"wipeout\":true} POST, exit 0; --mobile and missing passphrase → exit 2 with zero traffic; refused wipeout → exit 1 with detail; --wait seen-down-then-back over the http:// probe → exit 0 / exhaustion → exit 4; the http URL helper)"
  notes: >
    2026-10-09. hem-tool-core src/tools/hem-tool/wipe.{h,c} (implements:
    REQ-TOOL-022): login → ehem_system_config → identity printed (hostname,
    devid, instanceid, user) → the operator must type the hostname exactly
    (no --yes exists in the opts at all) → ehem_system_wipeout → "wipe
    accepted" + next steps; --wait polls a caller-supplied http:// probe
    context (main.c derives it with hem_wipe_http_url) until the device
    has been seen down and answers again, reporting "uninitialised; next:
    init-device". Registry entry (PASSPHRASE_ONLY, main section, after
    reboot) with the irreversibility named in details; main.c
    cmd_wipe_device + dispatch; the generic class guard refuses --mobile up
    front (exercised by hand: exit 2 + message). README passphrase-only
    list updated. NO live test (REQ-TEST-007); the attended wipe is the
    first act of STEP-M10-070. Verified: ./dev ci 44/44 gcc+clang (43 +
    test_wipe); ./dev test asan clean; ./dev check green; MinGW
    -fsyntax-only clean on wipe.c (Sleep/nanosleep split like recover.c).
    REQ-TOOL-022 → implemented (attended run pending).
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
- [x] Command + registry entry + per-command help (REQ-TOOL-020 gate:
      test_registry completeness walk passes).
- [x] Unit (tests/unit/test_wipe.c, fake transport): declined / wrong
      hostname → exit 3, zero writes; correct hostname → one wipeout
      request; `--yes` does not skip the prompt (no such option exists
      for this command); `--mobile` → exit 2; `--wait` success and
      exhaustion (exit 4).
- [x] No live test added; `./dev ci` + gates green; `implements:
      REQ-TOOL-022` tagged.
