---
id: REQ-SYS-007
title: Binding for /api/system/selftest — run the self-test battery and read repo stats
status: verified
priority: must
revision: 2
source: ARCHITECTURE.md §11 (M7: system group); encedo-hem-api-doc system/selftest.md; encedo_firmware api_system.c:260 api_get_system_selftest (fw v1.2.2); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/system/selftest — run the self-test battery and read repo stats

The SDK SHALL provide `ehem_system_selftest(ctx, &out)` over
`GET /api/system/selftest`, returning a caller-owned typed struct.

- **Side effect by design:** the endpoint synchronously re-runs the full
  battery (`self_check(0xFF)` — KAT, entropy, FLS) on every call; the
  binding documents this and the integration harness accounts for the
  latency (open criterion).
- Struct fields (tolerant parse; doc field table): `last_selftest_ts`,
  `last_fls_state`, `last_entropytest_ts`, `last_kat_ts`, `fls_state`
  (0 = nominal — the required field), `selftest_ts`, `kat_busy`
  (absent → false), `repo_stats` {total, deleted, invalid, fragmented,
  freeslots}, `se_state` (PPA-only; absent → −1). `repo_stats` is ONLY
  available here (not in status).
- **Auth:** any valid bearer, NO scope check (firmware "allowed JTW
  Scope: any"). The SDK requests **`system:config`** — a real firmware
  scope family, future-proof if the check tightens, and shared with the
  REQ-SYS-004 token. Open criterion records the any-scope behavior.
- Errors: 401 → auth path as usual; 500 → `EHEM_ERR_DEVICE`.

**Rationale:** M7 milestone "remaining system group". fls_state is the
device's own health verdict and repo_stats the only visibility into key
slot exhaustion — both feed the hem-tool selftest subcommand
(REQ-TOOL-013) and give integration suites a device-health probe.

**Acceptance criteria:**
- [x] Unit (fake transport): full-shape fixture parsed into the struct;
      minimal fixture (no kat_busy/se_state) → defaults false/−1;
      missing `fls_state` → `EHEM_ERR_PROTOCOL`; error mapping.
- [x] Live (my.ence.do fw v1.2.2-DIAG, 2026-07-18): fls_state == 0,
      selftest_ts sane, repo_stats present (total=4 keys, deleted=1554,
      fragmented=99, freeslots=1616 — heavy EHEMTEST churn history
      visible); **se_state present (=0) → the dev device is a PPA build**
      (ATECC secure enclave live) — the shared PPA/EPA determination for
      REQ-SYS-009/010/011. `kat_busy` observed true mid-run.
- [x] ~~OPEN~~ **RESOLVED (live 2026-07-18):** latency ≈ 5 s wall-clock —
      fits the 30 s default total timeout comfortably; no special
      guidance needed beyond the don't-poll-in-a-loop note.
- [x] ~~OPEN~~ **RESOLVED (live probe 2026-07-18, python-driven):** a
      `keymgmt:list`-scoped token IS accepted by selftest (any-scope
      confirmed, matching the firmware's "Scope: any"); the SDK keeps
      requesting `system:config`.
