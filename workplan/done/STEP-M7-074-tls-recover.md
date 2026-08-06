---
id: STEP-M7-074
title: "TLS recovery: ehem_tls_recover binding + hem-tool tls-recover"
milestone: M7
implements: ["REQ-SYS-013", "REQ-TOOL-015"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits: ["dcec047"]
  tests: ["verifies: REQ-SYS-013 (tests/unit/test_config.c — 3 cases), REQ-TOOL-015 (tests/unit/test_recover.c — 6 cases); live demo green 2026-07-22"]
  notes: >
    ehem_tls_recover in proto_system.c + system.h (EHEM_DEFAULT_REGISTER_URL
    default; attestation genuine → cloud register POST [absolute URL,
    EHEM_TLS_REQ_VERIFY, unauthenticated] → validate bundle has crt →
    splice VERBATIM into {"tls":...} → config POST system:config →
    {updated,reboot_required}). hem-tool-core recover.{h,c}: tls-recover
    [--force] (skip-if-healthy via status https flag → checkin → login →
    recover → reboot → poll until https:true; exit 0/1/2/3/4). Unit
    33/33 gcc+clang+ASan; gates green; MinGW cross-syntax OK on
    proto_system/recover/main. BUG CAUGHT: ehem_system_checkin rejects
    NULL out (EHEM_ERR_ARG), so the tool's clock-sync leg was a silent
    no-op — fixed to pass+free an ehem_checkin_info. LIVE (my.ence.do
    2026-07-22): skip path exit 0; --force full recovery reinstalled +
    rebooted → https://my.ence.do system-trusted (curl verify=0). This
    codifies the manual wipe-recovery done earlier the same session.
reopened: []
cancelled: null
---

**Goal:** `ehem_tls_recover(ctx, register_url, &out)` (attestation →
provisioning-cloud bundle → verbatim install, REQ-SYS-013) and
`hem-tool tls-recover [--force]` (skip-if-healthy → check-in → recover →
reboot → poll for `https: true`, REQ-TOOL-015), so the next TLS wipe is
a one-command recovery instead of a day of archaeology.

**Notes:** Mid-milestone insertion (user decision 2026-07-22), codifying
the recovery performed manually the same day. The cloud POST reuses the
check-in relay mechanism (absolute URL through the transport,
EHEM_TLS_REQ_VERIFY); the bundle's private key is encrypted to the
device's ATECC — the SDK relays verbatim and cannot read it. Reuses
ehem_cert_install_info for the result and the cert-install poll pacing
for the post-reboot wait (keyed on the status `https` flag flipping, so
the firmware's ~2 s keep-serving window cannot fake success). Live demo
plan: skip path on the healthy device, then one `--force` full pass
(reinstall + reboot — normal disruptive use on the disposable dev
device).

**Definition of done**
- [x] `ehem_tls_recover` exported, tagged `implements: REQ-SYS-013`;
      header doc covers the ceremony, the verbatim rule, and the
      default register endpoint
- [x] `hem-tool tls-recover [--force]` in hem-tool-core + main.c with
      exit codes 0/1/2/3/4 and `--help` text
- [x] Unit tests green (gcc+clang+asan): binding leg bodies incl.
      verbatim `tls` splice + no-crt PROTOCOL; tool skip/full/no-bundle/
      poll-exhaustion/usage paths
- [x] Live: skip path exits 0 on the healthy device; `--force` full
      recovery ends with the device serving HTTPS (system trust)
- [x] Export/header gates green; MinGW cross-syntax check on touched TUs
