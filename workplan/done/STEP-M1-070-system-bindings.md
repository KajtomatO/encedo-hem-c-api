---
id: STEP-M1-070
title: proto_system.c — status and version bindings with typed structs
milestone: M1
implements: ["REQ-SYS-001", "REQ-SYS-002", "REQ-API-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M1-030", "STEP-M1-050"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-SYS-001, REQ-SYS-002, REQ-API-005 (tests/unit/test_system.c)"]
  notes: >
    Public header include/ehem/system.h + src/proto_system.c. ehem_system_status
    → ehem_status_info, ehem_system_version → ehem_version_info (the data structs
    are suffixed _info because C shares one namespace for a typedef and a
    function, so the struct cannot share the getter's name). Both getters:
    build a GET request → send via ehem_ctx_transport with the context's
    per-request timeouts → map transport failure (surfacing curl's
    last_detail) and non-2xx HTTP (401→AUTH_FAILED, 403→SCOPE_DENIED,
    404→NOT_FOUND, other 4xx/5xx→DEVICE) onto ehem_rc with last-error detail
    (http_status + device body) → tolerant-parse the JSON via the M1-030 helper
    (unknown fields ignored; missing REQUIRED field → EHEM_ERR_PROTOCOL naming
    the field). Field sets taken from the live doc (fetched again 2026-07-15):
    status required = ctx, fls_state, uptime, temp; optional = ts, time
    (doc marks both "if RTC is set" — so treated OPTIONAL, a divergence from
    this step's original "requires ts, time" wording, to record at the M1-100
    gate against REQ-SYS-001's open criterion), storage[] (array of disk-status
    strings), hostname, format, fw_upgrade, inited, https, tts, repo_stats{}
    (auth-only). version required = hwv, fwv, blv; optional = fwk, fws, blk,
    bls, uis, sd_csd, sd_cid. Optional strings are NULL when absent; optional
    numbers/bools carry has_* flags. JSON helper gained array accessors
    (ehem_json_is_array / _array_size / _array_get / _as_string) for storage.
    ehem_ctx_fail lost its redundant payload_len arg (error payloads are always
    NUL-terminated). Matching NULL-safe ehem_system_status_free /
    ehem_system_version_free. Verified on Linux (2026-07-15): `ctest -L unit`
    8/8 green (test_system: full/minimal/missing-required/http-500/
    transport-error/malformed for status, full+missing-required for version,
    NULL-safe frees), ASan/LSan clean, `nm -D` exports the four new ehem_* symbols
    only. Live-shape confirmation vs the real device is STEP-M1-100.
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
- [x] Both bindings populate structs from canned doc-example JSON (unit tests) — *test_status_full, test_version_full*
- [x] Optional fields flagged present/absent; unknown fields ignored; missing required field → `EHEM_ERR_PROTOCOL` (unit tests) — *test_status_minimal_optionals_absent (+ unknown_future_field ignored), test_status_missing_required, test_version_missing_required*
- [x] Non-2xx response maps per REQ-API-003; detail via `ehem_last_error` (unit test) — *test_status_http_error (500→DEVICE, payload), test_status_transport_error*
- [x] `ehem_*_free()` for both structs, NULL-safe; ASan/LSan clean — *test_free_null_safe; ASan/LSan 8/8 clean*
