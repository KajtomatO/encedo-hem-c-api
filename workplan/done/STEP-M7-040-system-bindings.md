---
id: STEP-M7-040
title: "System bindings — selftest, attestation, shutdown"
milestone: M7
implements: ["REQ-SYS-007", "REQ-SYS-011", "REQ-SYS-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#9-testing-policy"]
depends_on: []
evidence:
  commits: ["3ad9db7"]
  tests: ["verifies: REQ-SYS-007/REQ-SYS-011/REQ-SYS-008 (tests/unit/test_config.c — 6 new cases; tests/integration/test_config_live.c — selftest + attestation live green 2026-07-18; shutdown live leg deliberately ABSENT)"]
  notes: >
    Unit 29/29 gcc+clang + ASan; export/header gates green; MinGW
    cross-syntax OK. Live (my.ence.do fw v1.2.2-DIAG): selftest
    fls_state=0, latency ≈5 s (fits default timeout), kat_busy observed
    true, repo_stats total=4/deleted=1554/fragmented=99/freeslots=1616;
    **se_state=0 present ⇒ dev device is a PPA build** (shared PPA/EPA
    determination for SYS-009/010/011). Any-scope probe (python-driven):
    keymgmt:list token accepted by selftest. Attestation: crt shape,
    leaf serial 8E CN "#0123fd3c60540abbee" — a DISTINCT identity from
    the TLS chain (recorded in REQ-SYS-011 rev2). Shutdown: unit-proven
    (empty-200 + full cache drop mirror of reboot, 409 kept); live =
    attended-manual only per REQ-SYS-008 — NOT run (recovery needs a
    physical power-cycle); its REQ open criterion stays open by design.
    REQ-SYS-007 + REQ-SYS-011 all other criteria resolved (rev2 each).
reopened: []
cancelled: null
---

**Goal:** `ehem_system_selftest` (typed battery result + repo_stats),
`ehem_system_attestation` (crt/csr variants + genuine), and
`ehem_system_shutdown` (empty-200 success, cache drop, physical-recovery
warning) in proto_system.c + system.h.

**Notes:** All three GETs; selftest + attestation share the
`system:config` token (REQ-SYS-007/011 scope choice), shutdown requests
`system:shutdown`. Shutdown mirrors the reboot binding's empty-2xx
convention and `ehem_auth_invalidate(ctx, NULL)` — copy REQ-SYS-005's
shape. Shutdown's live leg is NOT run (unit + attended-manual-only
policy per REQ-SYS-008 — record in evidence.notes; the disruptive suite
must not include it). Probes to record: selftest latency (fits default
timeout?), any-scope acceptance (unrelated-scope token), se_state
presence (PPA/EPA data point shared with M7-050), attestation crt
inspected via `ehem_cert_inspect`. Struct free functions follow the
API-005 ownership conventions; attestation strings are nullable.

**Definition of done**
- [x] Three bindings + free functions exported, tagged
      `implements: REQ-SYS-007` / `REQ-SYS-011` / `REQ-SYS-008`;
      shutdown header doc carries the physical-recovery warning
- [x] Unit tests green (gcc+clang+asan): selftest full/minimal shapes +
      PROTOCOL on missing fls_state; attestation both variants + 500
      "atecc_N" detail; shutdown empty-200 → OK + full cache
      invalidation (call-count), 403/409 mapping
- [x] Live: selftest fls_state==0, repo_stats present, latency +
      any-scope probes recorded in REQ-SYS-007 rev2; attestation crt
      leaf inspected, results in REQ-SYS-011 rev2; shutdown NOT run
      (evidence.notes states the policy)
- [x] Export/header gates green; MinGW cross-syntax check run
