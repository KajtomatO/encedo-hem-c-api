---
id: STEP-M10-070
title: "M10 gate: attended wipe → init → recovery cycle on the dev device; attended-only check; MFW re-check"
milestone: M10
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M10-030", "STEP-M10-040", "STEP-M10-060", "STEP-M10-065", "STEP-M10-068"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore, `implements: []` — it verifies
REQ-TEST-007 and provides the attended evidence for REQ-SYS-014,
REQ-AUTH-011, REQ-TOOL-021 and REQ-TOOL-022). In one attended sitting on
the dev device: `hem-tool wipe-device --wait` (typed hostname) → device
back over http, uninitialised → `hem-tool --url http://… init-device …`
with the test passphrase (so `hem.env` keeps working) → `hem-tool
recovery` takes the tls-recover path and restores HTTPS → `hem-tool
status` under system trust and a passphrase login succeed. Each
observation is recorded in the owning REQ (date, device, outcome) and
in this step's `evidence.notes`.

**Notes:** **The wipe destroys everything on the device:** all keys, the
TLS material, the logs and the resident phone pairing (the user's phone,
protected-labeled since M8) — re-pair afterwards with `ext pair` if
mobile auth should keep working, or accept its loss. The user decides
at the sitting; nothing here runs unattended. REQ-TEST-007 check: grep
`ehem_system_wipeout` / `ehem_device_init` under tests/integration and
tests/disruptive → nothing. MFW re-check (the M10 decomposition chore):
firmware is still v1.2.2 — confirm with `hem-tool status` and record
"MFW unchanged" (or promote anything the firmware now routes). Then
§4.3 regeneration (the four attended-only REQs transition on their
recorded evidence, REQ-TEST-007's convention), ARCHITECTURE §11 M10
marked gate-passed, KNOWN-ISSUES updated with anything the cycle
surfaced. Commits/tags are the user's.

**Definition of done**
- [ ] Attended cycle completed and recorded: wipe (REQ-SYS-014 +
      REQ-TOOL-022 evidence), init (REQ-AUTH-011 + REQ-TOOL-021), recovery
      restored HTTPS (REQ-TOOL-023 case 3), status + login green.
- [ ] REQ-TEST-007: no live test for init/wipe in the tree (grep); the
      attended records are in place.
- [ ] MFW re-checked against the running firmware; result recorded in
      ARCHITECTURE §11.
- [ ] Fresh `./dev ci` (gcc + clang) + ASan + gates green; `./dev test it`
      integration sweep green on the re-initialised device.
- [ ] Release artifacts of the gate commit (STEP-M10-065 workflow)
      downloaded — the hem-tool archive and the wolfSSL companion asset
      unpacked side by side — and smoke-run (`hem-tool status`, system
      trust) on Linux and Windows — attended. The licensing decision is
      recorded (REQ-BUILD-005 rev 2, user decision 2026-10-09: wolfSSL
      dynamic, never bundled statically; rev 2 re-approved the same day).
- [ ] TRACE.md regenerated (§4.3); ARCHITECTURE §11 M10 gate-passed.
