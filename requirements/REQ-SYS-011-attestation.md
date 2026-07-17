---
id: REQ-SYS-011
title: Binding for /api/system/config/attestation — device attestation material
status: approved
priority: should
revision: 1
source: ARCHITECTURE.md §11 (M7: system group); encedo-hem-api-doc system/config-attestation.md, system/config-provisioning.md (provisioning EXCLUDED — factory-only, 403 once initialised); encedo_firmware api_system.c:2285 api_get_system_config_attestation (fw v1.2.2); approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003", "REQ-SYS-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Binding for /api/system/config/attestation — device attestation material

The SDK SHALL provide `ehem_system_attestation(ctx, &out)` over
`GET /api/system/config/attestation`, returning the ATECC608 attestation
material in a caller-owned struct with nullable variant fields.

- **Two response shapes** (tolerant parse, doc + handler):
  - provisioned chip (the normal path): `{crt, genuine}` — `crt` is the
    base64-DER device certificate, inspectable with the existing public
    `ehem_cert_inspect` (REQ-SYS-006);
  - fresh chip: `{csr, key, genuine}` — PEM CSR + base64-DER attestation
    public key.
  The struct exposes `crt_b64`, `csr_pem`, `key_b64` (each NULL when
  absent) and `genuine` (always present — attestation token proving a
  real ATECC608; missing → `EHEM_ERR_PROTOCOL`).
- **Auth:** any valid bearer, no scope check (firmware limits tracking,
  not capability); the SDK requests `system:config` (same choice and
  rationale as REQ-SYS-007, shares that token).
- PPA-only; absent route → 404 → `EHEM_ERR_NOT_FOUND`, tests skip.
- Errors: 409 (install busy / fls_state) → `EHEM_ERR_DEVICE`; 500 with
  `"atecc_N"` body (secure-element I/O failure) → `EHEM_ERR_DEVICE`
  with the body preserved in detail.
- **`POST /api/system/config/provisioning` is deliberately NOT bound:**
  factory-first-run-only (403 on any initialised device, one-shot per
  ATECC608) — zero consumer value for an SDK client; recorded here so
  the M9 sweep doesn't re-litigate it.

**Rationale:** M7 milestone "remaining system group". Attestation lets a
consumer prove device genuineness (CC-evaluation material); the cert
path reuses the REQ-SYS-006 inspection surface rather than growing a new
ASN.1 seam.

**Acceptance criteria:**
- [ ] Unit (fake transport): both shapes parsed (crt-variant,
      csr-variant); absent `genuine` → `EHEM_ERR_PROTOCOL`; 500 body
      `"atecc_1"` lands in last-error detail; 404/409 mapping.
- [ ] Live: attestation on my.ence.do → `crt` variant expected
      (provisioned device); `ehem_cert_inspect(crt_b64)` yields a
      parseable leaf (serial/CN recorded); `genuine` non-empty;
      PPA/EPA routing recorded (shared data point with REQ-SYS-009/010).
- [ ] OPEN (live probe): whether the attestation cert differs from the
      TLS cert chain (expected: yes — ATECC slot cert vs flash TLS
      cert); recorded for consumer guidance.
