---
id: STEP-M1-070
title: proto_system.c — status and version bindings with typed structs
milestone: M1
implements: ["REQ-SYS-001", "REQ-SYS-002", "REQ-API-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M1-030", "STEP-M1-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_system_status()` and `ehem_system_version()` returning
caller-owned typed structs (with present/absent indication for optional
fields) released by matching `ehem_*_free()` functions — the first real
protocol bindings, fully unit-tested through the fake transport.

**Notes:** Field sets per encedo-hem-api-doc `system/status.md` and
`system/version.md` (fetched 2026-07-15): status requires `ctx`,
`fls_state`, `uptime`, `ts`, `time`, `temp`, `storage` with optional
`hostname`, `inited`, `https`, `fw_upgrade`, `repo_stats`, ...; version
has `hwv`, `fwv`, `blv`, key/signature blobs, optional `uis`/`sd_*`.
Tolerant parsing via the STEP-M1-030 helpers: unknown fields ignored,
missing required → `EHEM_ERR_PROTOCOL` with detail. Real-device shape
check happens at STEP-M1-100; struct adjustments feed back into
REQ-SYS-001/-002.

**Definition of done**
- [ ] Both bindings populate structs from canned doc-example JSON (unit tests)
- [ ] Optional fields flagged present/absent; unknown fields ignored; missing required field → `EHEM_ERR_PROTOCOL` (unit tests)
- [ ] Non-2xx response maps per REQ-API-003; detail via `ehem_last_error` (unit test)
- [ ] `ehem_*_free()` for both structs, NULL-safe; ASan/LSan clean
