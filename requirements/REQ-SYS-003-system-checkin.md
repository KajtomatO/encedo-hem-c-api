---
id: REQ-SYS-003
title: Binding for the /api/system/checkin handshake (device + cloud relay)
status: approved
priority: must
revision: 1
source: user decision 2026-07-15 ("check-in is critical"); encedo-hem-api-doc system/checkin.md; encedo-hem-python-api transport.py/system.py (relay semantics, cloud TLS posture)
depends_on: ["REQ-API-001", "REQ-NET-001", "REQ-API-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#7-transport"]
---

# Binding for the /api/system/checkin handshake (device + cloud relay)

The SDK SHALL provide `ehem_system_checkin()` implementing the three-leg
check-in handshake — the mechanism by which the device verifies firmware,
sets its RTC, and **refreshes its TLS certificate** from the Encedo cloud:

1. `GET /api/system/checkin` on the device (no auth) → `{"check": ...}`;
2. `POST` the leg-1 response **verbatim** to the cloud check-in endpoint
   (default `https://api.encedo.com/checkin`, overridable via context
   options for testing), `Content-Type: application/json` → `{"checked": ...}`;
3. `POST` the leg-2 response **verbatim** to `/api/system/checkin` on the
   device → `{newcrt, newfws, newuis, status}` returned as a typed,
   caller-owned `ehem_checkin_result` freed by `ehem_checkin_result_free()`.

**Rationale:** The M1 gate found the dev device's certificate expired; only a
check-in renews it. The flow requires an external relay (the device cannot
reach the cloud itself), so the SDK performs both sides. Response fields are
treated as optional strings (tolerant parsing) since firmware variants may
omit them; `cert_updated` is derived from a non-empty `newcrt`.

**Security constraints:**
- The **cloud leg (leg 2) MUST use full TLS verification** regardless of the
  context's TLS mode — the check-in data is the trust anchor being delivered;
  relaying it via an unverified connection would expose the flow to MITM
  (matches the python client's mandatory `verify=True` for the backend).
- The **device legs (1 and 3) MAY relax certificate verification** only
  within the check-in flow, since they must work precisely when the device's
  certificate is invalid, and the payload is cloud-signed data the device
  itself validates.

**Acceptance criteria:**
- [ ] `ehem_system_checkin(ctx, &out)` performs the three legs with verbatim
      body relay and correct Content-Type (unit test via fake transport
      asserting the full request sequence).
- [ ] Leg 2 goes to the configured cloud URL with TLS verification forced on;
      legs 1/3 may run with relaxed verification only inside this flow (unit
      test asserts the per-request TLS override values).
- [ ] `ehem_checkin_result` fields are optional (tolerant parsing);
      `ehem_checkin_result_free(NULL)` is a no-op; ASan/LSan clean.
- [ ] Device/cloud errors map per REQ-API-003 with `ehem_last_error` detail
      (device: 400 bad args, 401 validation failed per the doc).
- [x] RESOLVED (live, 2026-07-15): three-leg flow verified against the real
      dev-machine HEM (fw v1.2.2-DIAG) and https://api.encedo.com —
      test_checkin_live green; `hem-tool checkin` reports `status: OK`,
      `cert refreshed: yes` (`newcrt` present). Response shape matches the doc.
      **Device-behavior finding:** the device *accepts* the certificate update
      (status OK, newcrt non-empty) but **keeps serving the old certificate on
      its running TLS server** — at gate time the served cert (expired
      2026-04-18) was unchanged after repeated successful check-ins. Either
      the device applies certificates only on reboot/TLS restart, or the cloud
      is re-delivering the same stale certificate; indistinguishable from
      outside. Consequence handled in REQ-NET-005: an effective-refresh check
      after recovery, with an explanatory error when the device still serves
      the old cert. Re-verify (and record which hypothesis held) after the
      device is next rebooted (reboot binding lands in M7, or manual).
