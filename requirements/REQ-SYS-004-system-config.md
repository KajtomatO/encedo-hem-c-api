---
id: REQ-SYS-004
title: Binding for /api/system/config — read config and install a TLS certificate
status: approved
priority: must
revision: 1
source: user decision 2026-07-16 (cert-install tooling); approved 2026-07-16; encedo-hem-api-doc system/config.md; live remediation 2026-07-16 (config tls.crt install verified working on fw v1.2.2)
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

**Acceptance criteria:**
- [ ] Config GET declares scope `system:config`, parses required + optional
      fields tolerantly, `_free` is NULL-safe (fake-transport unit tests;
      ASan/LSan clean).
- [ ] Cert install POSTs exactly `{"tls":{"crt": …}}`, declares scope
      `system:config`, surfaces `updated`/`reboot_required` (unit test).
- [ ] Device errors map per REQ-API-003 (400 validator failure → device
      payload in `ehem_last_error`; 409 install-in-progress → `EHEM_ERR_DEVICE`
      with detail) (unit test).
- [ ] OPEN (M2 gate): live `GET /api/system/config` against the dev device
      returns the known hostname `my.ence.do`; record the response shape.
