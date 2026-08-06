---
id: REQ-SYS-004
title: Binding for /api/system/config — read config and install a TLS certificate
status: verified
priority: must
revision: 2
source: user decision 2026-07-16 (cert-install tooling); approved 2026-07-16; encedo-hem-api-doc system/config.md; live remediation 2026-07-16 (config tls.crt install verified working on fw v1.2.2); rev 2 = M9 sweep records (2026-08-06)
depends_on: ["REQ-AUTH-003", "REQ-API-005", "REQ-SYS-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/system/config — read config and install a TLS certificate

The SDK SHALL provide, for an initialised device (scope `system:config`,
token `sub` U or M):

1. `ehem_system_config(ctx, &out)` — `GET /api/system/config` parsed into a
   typed, caller-owned struct (tolerant parsing per ARCHITECTURE §6:
   required core fields `devid`, `hostname`, `user`; everything else
   optional with `has_*`/NULL conventions; unknown fields ignored).
2. `ehem_system_config_install_cert(ctx, crt_b64)` —
   `POST /api/system/config` with body `{"tls":{"crt":"<base64 DER
   chain>"}}` (the doc's cert-only variant: replaces the stored certificate,
   keeps the stored private key). Returns the parsed
   `{updated, reboot_required}` so callers know a reboot must follow.

M2 scope is deliberately narrow: general config writes (user, email, device
options, storage layout, userkey rotation) remain M7+ territory; only the
TLS-cert install variant is bound because check-in cert rotation is broken
in device firmware v1.2.2 (REQ-SYS-003 root-cause finding) and this path is
the working remediation (executed live 2026-07-16).

**M9 conformance-sweep records (2026-08-06, docs/COVERAGE.md):**
1. The narrow POST scope is now the RATIFIED 1.0 disposition, not an
   interim one: general config/administration writes — including the
   documented `wipeout` (factory reset), `userkey*` rotation, and
   `gen_csr` — stay deliberately unbound (no HEM-SDK-1..9 consumer need;
   M10 candidate if device administration is ever wanted).
2. DISCREPANCIES-OFFICIAL-DOCS records that firmware answers an
   UNAUTHENTICATED `GET /api/system/config` with an identifying subset
   (`eid` etc.) for pairing/discovery clients, despite the doc's
   bearer-required marking. NOT live-verified by this SDK and not relied
   on — the SDK gets `eid` from the auth challenge (REQ-AUTH-001).
   Recorded as a sweep hook for any future discovery feature.

**Acceptance criteria:**
- [ ] Config GET declares scope `system:config`, parses required + optional
      fields tolerantly, `_free` is NULL-safe (fake-transport unit tests;
      ASan/LSan clean).
- [ ] Cert install POSTs exactly `{"tls":{"crt": …}}`, declares scope
      `system:config`, surfaces `updated`/`reboot_required` (unit test).
- [ ] Device errors map per REQ-API-003 (400 validator failure → device
      payload in `ehem_last_error`; 409 install-in-progress → `EHEM_ERR_DEVICE`
      with detail) (unit test).
- [x] RESOLVED (STEP-M2-050, 2026-07-16): live `GET /api/system/config` via
      the C SDK (tests/integration/test_config_live.c) returns hostname
      `my.ence.do` (devid `3dfd39eb56787905`, user `usb C`). Response shape
      (fw v1.2.2): `devid, hostname, user, email (may be ""), eid, eid_sign,
      instanceid, origin, ip, genuine_id` (strings); `iat, uts, ctx,
      storage_mode, storage_disk0size, storage_capacity,
      http_option_dosprot_mode` (numbers); `dnsd, trusted_ts, trusted_backend,
      allow_keysearch, http_option_hsts` (booleans); plus session-crypto `spk`
      and `nonce` (deliberately NOT surfaced). The SDK types the core and
      ignores the rest tolerantly.
