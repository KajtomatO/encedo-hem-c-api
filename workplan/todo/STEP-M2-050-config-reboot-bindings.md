---
id: STEP-M2-050
title: "System config + reboot bindings (GET config, TLS cert install POST, reboot)"
milestone: M2
implements: ["REQ-SYS-004", "REQ-SYS-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M2-040"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** In `proto_system.c` + `include/ehem/system.h`:
`ehem_system_config(ctx, &out)` (typed `ehem_config_info`, tolerant
parsing, `_free`), `ehem_system_config_install_cert(ctx, crt_b64, &out)`
(`{"tls":{"crt":…}}`, returns updated/reboot_required flags), and
`ehem_system_reboot(ctx)` (drops the token cache on success). All declare
scope `system:config` (single scope acquisition serves all three —
REQ-SYS-005 scope note).

**Notes:** Config GET response has ~25 fields; type the core (devid,
hostname, user, email, iat/uts, options booleans, storage numbers) with
has_*/NULL discipline like `ehem_status_info` — skip `spk`/`nonce`
(session-crypto internals, no SDK use case yet; note in header). 409
(install in progress) → EHEM_ERR_DEVICE with detail. Reboot: no response
body expected; treat transport-level connection drop right after 200 as
success (device may kill the socket).

**Definition of done**
- [ ] Fake-transport unit tests: config GET parse (full + minimal
      responses), install POST body shape, scope declarations, 400/409
      mapping, reboot cache-drop (subsequent scoped call re-acquires).
- [ ] `_free` functions NULL-safe; ASan/LSan clean.
- [ ] Live (non-disruptive part): `GET /api/system/config` against the dev
      device returns hostname `my.ence.do` — record shape in REQ-SYS-004
      open criterion. Reboot live run deferred to M2-060/gate (disruptive).
- [ ] Export + header checks green.
