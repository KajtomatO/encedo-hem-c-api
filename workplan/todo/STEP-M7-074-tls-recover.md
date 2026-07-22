---
id: STEP-M7-074
title: "TLS recovery: ehem_tls_recover binding + hem-tool tls-recover"
milestone: M7
implements: ["REQ-SYS-013", "REQ-TOOL-015"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `ehem_tls_recover` exported, tagged `implements: REQ-SYS-013`;
      header doc covers the ceremony, the verbatim rule, and the
      default register endpoint
- [ ] `hem-tool tls-recover [--force]` in hem-tool-core + main.c with
      exit codes 0/1/2/3/4 and `--help` text
- [ ] Unit tests green (gcc+clang+asan): binding leg bodies incl.
      verbatim `tls` splice + no-crt PROTOCOL; tool skip/full/no-bundle/
      poll-exhaustion/usage paths
- [ ] Live: skip path exits 0 on the healthy device; `--force` full
      recovery ends with the device serving HTTPS (system trust)
- [ ] Export/header gates green; MinGW cross-syntax check on touched TUs
