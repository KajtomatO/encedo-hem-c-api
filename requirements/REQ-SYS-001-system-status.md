---
id: REQ-SYS-001
title: Binding for GET /api/system/status
status: verified
priority: must
revision: 1
source: encedo-hem-api-doc system/status.md (fetched 2026-07-15); ARCHITECTURE.md §11 M1
depends_on: ["REQ-API-001", "REQ-NET-001", "REQ-API-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
---

# Binding for GET /api/system/status

The SDK SHALL provide a binding for `GET /api/system/status` returning the
device status as a typed, caller-owned C struct, callable without prior
authentication.

**Rationale:** First live endpoint; doubles as the reachability check the
consumer needs for token-presence reporting (HEM-SDK-1). Per the API doc
(system/status.md) the endpoint requires no authentication (a token only
adds key-repository statistics); documented response fields include `ctx`,
`fls_state`, `uptime`, `ts`, `time`, `temp`, `storage`, plus optional
fields (`hostname`, `inited`, `https`, `fw_upgrade`, `repo_stats`, ...).

**Acceptance criteria:**
- [ ] `ehem_system_status(ctx, &out)` populates a typed struct from the
      documented response fields, with optional fields carrying a
      present/absent indication; freed by its `ehem_*_free()` (unit test
      with canned JSON via fake transport).
- [ ] Unknown JSON fields are ignored (tolerant parsing, §6); a missing
      required field yields `EHEM_ERR_PROTOCOL` with detail.
- [ ] Non-2xx HTTP responses map to `ehem_rc` per REQ-API-003.
- [x] RESOLVED (M1 gate, 2026-07-15): verified against the real dev-machine
      HEM (fw `v1.2.2-DIAG`). Live `GET /api/system/status` returned
      `{ctx, uptime, ts, time, storage, temp, fls_state}`. All four fields the
      binding treats as required (`ctx`, `fls_state`, `uptime`, `temp`) were
      present → OK. `ts`/`time` were present (RTC set), confirming it is safe to
      treat them OPTIONAL (the doc marks them "if RTC is set"). `storage` is an
      array of `"<bytes>:<flag>"` strings (e.g. `"8388607:ro"`, `"234364924:-"`),
      not the doc's `"disk0_status"` placeholder — parsed fine as strings.
      Optional `hostname`/`inited`/`https`/`fw_upgrade`/`format`/`tts`/`repo_stats`
      were absent on this DIAG firmware (`repo_stats` needs auth) and correctly
      reported absent. No struct or required-field change needed.
