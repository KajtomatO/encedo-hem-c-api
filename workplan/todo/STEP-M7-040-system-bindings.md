---
id: STEP-M7-040
title: "System bindings — selftest, attestation, shutdown"
milestone: M7
implements: ["REQ-SYS-007", "REQ-SYS-011", "REQ-SYS-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#9-testing-policy"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Three bindings + free functions exported, tagged
      `implements: REQ-SYS-007` / `REQ-SYS-011` / `REQ-SYS-008`;
      shutdown header doc carries the physical-recovery warning
- [ ] Unit tests green (gcc+clang+asan): selftest full/minimal shapes +
      PROTOCOL on missing fls_state; attestation both variants + 500
      "atecc_N" detail; shutdown empty-200 → OK + full cache
      invalidation (call-count), 403/409 mapping
- [ ] Live: selftest fls_state==0, repo_stats vs keys-list consistency,
      latency + any-scope probes recorded in REQ-SYS-007; attestation
      crt leaf inspected, results in REQ-SYS-011; shutdown NOT run
      (evidence.notes states the policy)
- [ ] Export/header gates green; MinGW cross-syntax check run
