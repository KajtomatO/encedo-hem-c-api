---
id: REQ-TEST-007
title: Attended-only verification class — no live CTest for device init and wipe
status: approved
priority: must
revision: 1
source: user decision 2026-10-07 ("both init and wipe can be tested only manually"); ARCHITECTURE.md §9 (attended-only bullet); approved 2026-10-07 (M10 decomposition, user go-ahead)
depends_on: ["REQ-TEST-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
---

# Attended-only verification class — no live CTest for device init and wipe

Bindings and hem-tool commands classified **attended-only** in
ARCHITECTURE.md §9 — device initialisation (REQ-AUTH-011, REQ-TOOL-021)
and device wipe (REQ-SYS-014, REQ-TOOL-022) — SHALL have no live test in
the CTest tree at all: not under `integration`, not under `disruptive`.
Their live verification is an attended run whose date, device and
outcome are recorded in the owning REQ's acceptance criteria and in the
implementing step's `evidence.notes`; their offline fake-transport unit
tests remain mandatory (ARCHITECTURE §9).

**Rationale:** both operations destroy or create the device's identity
(configuration, user keys, key repository, TLS material). The existing
`disruptive` label still allows an opt-in automated run; the user ruled
that out for these two operations (2026-10-07) — a human must be present
and decide each time.

**Acceptance criteria:**
- [ ] No file under `tests/integration/` or `tests/disruptive/` calls
      `ehem_system_wipeout` or `ehem_device_init` (checked at the M10
      gate; a grep gate in the unit suite MAY enforce it).
- [ ] REQ-AUTH-011, REQ-SYS-014, REQ-TOOL-021 and REQ-TOOL-022 each carry
      an attended-run record (date, device, outcome) before they are
      marked verified; the §4.3 procedure treats that record as the
      "passing test" for these four REQs (recorded exception, like the
      TOOL-001/002/014 live/manual convention).
- [ ] The `disruptive` label's documentation (tests/README.md,
      ARCHITECTURE §9) names the attended-only class so nobody adds
      such a test later.
