---
id: STEP-M2-050
title: "System config + reboot bindings (GET config, TLS cert install POST, reboot)"
milestone: M2
implements: ["REQ-SYS-004", "REQ-SYS-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M2-040"]
evidence:
  commits: []   # implemented in the working tree; awaiting the user's commit
  tests:
    - "verifies: REQ-SYS-004, REQ-SYS-005 — tests/unit/test_config.c (10 cases: config GET full parse incl. every typed field + has_* flags + spk/nonce ignored, minimal parse, missing-required → PROTOCOL; cert-install POST body byte-exact {\"tls\":{\"crt\":..}} + updated/reboot_required, reboot_required-absent → false, NULL out ok, 400 validator → EHEM_ERR_DEVICE w/ payload, 409 → EHEM_ERR_DEVICE; reboot accepts empty 200 + drops the token cache so a later scoped call re-acquires, reboot 403 keeps the cache; _free NULL-safe)"
    - "verifies: REQ-SYS-004 live — tests/integration/test_config_live.c (public API only, shared lib, gated): login + ehem_system_config → hostname my.ence.do. GREEN against my.ence.do 2026-07-16 (devid 3dfd39eb56787905, user 'usb C')."
  notes: |
    proto_system.c + include/ehem/system.h: three authenticated bindings, all
    scope "system:config" (single acquisition serves all — REQ-SYS-005 note).
    Firmware-grounded (encedo_firmware api_system.c / crypto.c) + live-probed
    before coding.

    ehem_system_config — GET /api/system/config → typed ehem_config_info
    (tolerant, like ehem_status_info): required devid/hostname/user; optional
    strings email(may be "")/eid/instanceid/origin/ip/genuine_id; optional
    numbers iat/uts/ctx/storage_mode/storage_disk0size/storage_capacity;
    optional bools dnsd/trusted_ts/trusted_backend/allow_keysearch/http_hsts
    (json key http_option_hsts). Session-crypto spk/nonce and niche eid_sign /
    http_option_dosprot_mode deliberately NOT surfaced (ignored tolerantly;
    noted in the header). _free NULL-safe.

    ehem_system_config_install_cert(ctx, crt_b64, &out) — POST
    {"tls":{"crt":<crt_b64>}} built through a NEW json helper
    ehem_json_add_object (nested object; keeps cJSON in json.c per REQ-BUILD-003).
    Parses {updated, reboot_required} into ehem_cert_install_info (heap + free,
    consistent with the other output structs; out may be NULL). 400 validator /
    409 in-progress → EHEM_ERR_DEVICE with device payload (proto map: any ≥400
    non-401/403/404 → DEVICE).

    ehem_system_reboot(ctx) — GET /api/system/reboot. Firmware answers
    `200 | 1024` (HTTP 200, EMPTY body, close socket) then reboots ~2 s later
    (api_get_system_reboot). The shared path flags an empty 2xx body as
    EHEM_ERR_PROTOCOL with http_status 200, which the binding treats as success
    (the only place that signal means "ok"). On success it drops the WHOLE token
    cache via the new ehem_auth_invalidate(ctx, NULL) — a reboot invalidates
    every issued token (REQ-SYS-005); retained passphrase re-logs-in on the next
    call. Chose 2xx-only = success over the step's "treat a bare connection drop
    as success" because the firmware sends the 200 first, so a transport error
    genuinely means the reboot was not accepted (false-success is the unsafe
    direction). Reboot is DISRUPTIVE — no live run here (deferred to the
    disruptive-gated M2 gate); unit-tested via the fake.

    Live config shape recorded in REQ-SYS-004 (criterion RESOLVED). Verified:
    ./dev ci 13/13 gcc+clang under -Werror; ./dev test asan clean;
    export_symbols green (5 new ehem_* exported; ehem_json_add_object stays
    internal/hidden); public_headers_dep_free green. test_config_live GREEN live,
    skips cleanly without creds.
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
- [x] Fake-transport unit tests: config GET parse (full + minimal
      responses), install POST body shape, scope declarations, 400/409
      mapping, reboot cache-drop (subsequent scoped call re-acquires).
- [x] `_free` functions NULL-safe; ASan/LSan clean.
- [x] Live (non-disruptive part): `GET /api/system/config` against the dev
      device returns hostname `my.ence.do` — recorded shape in REQ-SYS-004
      (criterion RESOLVED). Reboot live run deferred to M2-060/gate (disruptive).
- [x] Export + header checks green.
