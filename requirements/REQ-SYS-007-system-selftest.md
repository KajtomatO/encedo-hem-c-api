---
id: REQ-SYS-007
title: Binding for /api/system/selftest — run the self-test battery and read repo stats
status: approved
priority: must
revision: 1
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
- [ ] Unit (fake transport): full-shape fixture parsed into the struct;
      minimal fixture (no kat_busy/se_state) → defaults false/−1;
      missing `fls_state` → `EHEM_ERR_PROTOCOL`; error mapping.
- [ ] Live: selftest on my.ence.do → fls_state == 0, selftest_ts sane
      (post-checkin clock), repo_stats.total consistent with
      `ehem_key_list`'s total (±0 — same repo); se_state
      presence/absence recorded (PPA/EPA data point).
- [ ] OPEN (live probe): call latency measured and recorded (synchronous
      KAT re-run — does it fit the default 30 s timeout or does the
      binding doc recommend a longer per-context timeout?).
- [ ] OPEN (live probe): a token with an unrelated scope (e.g.
      `keymgmt:list`) is accepted (any-scope confirmed) — recorded, SDK
      keeps requesting `system:config`.
