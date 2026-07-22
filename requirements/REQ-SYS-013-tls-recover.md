---
id: REQ-SYS-013
title: TLS recovery binding — obtain and install a full key+cert bundle from the provisioning cloud
status: approved
priority: should
revision: 1
source: user decision 2026-07-22 ("add a recovery function to hem-tool so it will be easier in future" — after the wipe recovery performed manually the same day); Encedo provisioning tool (tmp/provis2.htm getFactoryCert); encedo_firmware api_system.c:1345 (tls emp/key/crt bundle install path); live-proven flow 2026-07-22
depends_on: ["REQ-SYS-011", "REQ-SYS-004", "REQ-AUTH-003", "REQ-NET-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#7-transport"]
---

# TLS recovery binding — obtain and install a full key+cert bundle from the provisioning cloud

The SDK SHALL provide `ehem_tls_recover(ctx, register_url, &out)`: fetch
the device's attestation, present its `genuine` token to the Encedo
provisioning cloud, and install the returned TLS bundle on the device —
restoring HTTPS on a device that has lost its TLS key material entirely.

- **Why it exists:** a device wipe deletes the stored `TLS PrivateKey` /
  `TLS Certificate`, after which `httpsd` cannot start and the device is
  HTTP-only. `cert-install` (REQ-SYS-003/006/TOOL-003) cannot help — it
  installs a CERT-ONLY blob against an existing key. The provisioning
  cloud closes the gap: given a valid ATECC `genuine` token it returns a
  full bundle `{emp, key, crt, ...}` in which the fresh TLS PRIVATE key
  is AES-encrypted to the device's secure element (the firmware ECDHs
  its ATECC slot-0 key against `emp` to decrypt — api_system.c:1345).
  The SDK relays the bundle VERBATIM and can never read the key.
- **Flow:** `ehem_system_attestation` (REQ-SYS-011) → POST
  `{"genuine": …}` to `register_url` (NULL → the factory default
  `https://api.encedo.com/domain/register/my`; devices on custom domains
  pass their own register endpoint) — a cloud call over the transport's
  absolute-URL path, always fully TLS-verified (`EHEM_TLS_REQ_VERIFY`),
  exactly like the check-in relay leg — → validate the response is a
  JSON object containing `crt` → POST `{"tls": <bundle verbatim>}` to
  `/api/system/config` (scope `system:config`) → return the
  `{updated, reboot_required}` result (the existing
  `ehem_cert_install_info`). The caller reboots (REQ-SYS-005) to start
  `httpsd` with the new material.
- Works over an `http://` device URL by design — that is the state the
  device is in when this binding is needed.
- Errors: attestation/cloud/config failures map per the shared path
  (cloud 4xx payload preserved); a response that is not a JSON object
  with `crt` is `EHEM_ERR_PROTOCOL` ("the cloud delivered no usable
  bundle").

**Rationale:** performed manually on 2026-07-22 after the device wipe
(token → attestation → domain/register/my → config tls → reboot →
`"https": true` with the publicly-valid my.ence.do cert). Codifying it
makes wipe recovery a one-command operation and keeps hem-tool
public-API-only (the cloud call needs SDK transport machinery). Baking
the provisioning-cloud endpoint into the SDK follows the
EHEM_DEFAULT_CHECKIN_URL precedent.

**Acceptance criteria:**
- [x] Unit (fake transport, tests/unit/test_config.c, 2026-07-22):
      attestation GET; register POST to the DEFAULT URL with
      `{"genuine":…}` and to a caller URL when given; config POST whose
      `tls` value is the cloud response VERBATIM with a bearer (register
      itself unauthenticated); `{updated, reboot_required}` parsed; a
      bundle without `crt` → `EHEM_ERR_PROTOCOL` and no config POST.
- [x] Live (2026-07-22): the binding restored HTTPS on the real device
      via the `hem-tool tls-recover --force` demo (REQ-TOOL-015) —
      attestation → domain/register/my → config `tls` install → reboot
      → `https://my.ence.do` serving a system-trusted cert
      (curl verify=0).
