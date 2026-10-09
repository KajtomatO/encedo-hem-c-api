---
id: REQ-AUTH-011
title: Device initialisation binding — /api/auth/init challenge and signed cfg commit
status: implemented
priority: should
revision: 2
source: user decision 2026-08-07 (auth/init moved into M10: "the SDK should be able to initialise a wiped device"); user decision 2026-10-07 (M10 scope, attended-only); ARCHITECTURE.md §11 (M10); Encedo Manager assets/build.js initFinal (build.js:655-770 at b33c236 — the authoritative client flow); encedo-hem-api-doc auth/init.md; encedo_firmware api_auth.c:330-368 (GET) and :369-830 (POST; completeness mask 0x3FFF at :705); approved 2026-10-07 (M10 decomposition, user go-ahead)
depends_on: ["REQ-AUTH-001", "REQ-AUTH-002", "REQ-API-005", "REQ-SYS-003", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#11-milestones"]
---

# Device initialisation binding — /api/auth/init challenge and signed cfg commit

The SDK SHALL provide `ehem_device_init(ctx, const ehem_init_params *,
ehem_init_info **out)` implementing the two-step personalisation of an
uninitialised device exactly as the Manager's `initFinal` does it:

1. `GET /api/auth/init` (no auth) → challenge `{exp, spk, jti, genuine,
   eid}`;
2. **user persona:** PBKDF2-HMAC-SHA256(passphrase, salt = the `eid`
   string, 600 000 iterations, 32 bytes) → X25519 keypair →
   `cfg.userkey` — REQ-AUTH-001's derivation, so the same passphrase
   logs in afterwards (`sub="U"`);
3. **master persona:** the caller's 32-byte master secret → X25519
   keypair → `cfg.masterkey`;
4. ECDH(master private key, `spk`) → shared secret; eJWT header
   `{"ecdh":"x25519"}` (the Manager's minimal header — the firmware reads
   only `ecdh`, REQ-AUTH-001), claims `{jti, aud: spk, exp: the
   challenge's exp, iat, iss: master public key (standard base64), cfg}`,
   HMAC-SHA256 tag keyed with the raw shared secret. **The init JWT is
   signed by the master key** (`iss` = masterkey, `build.js:705-717`),
   not by the user key;
5. `POST /api/auth/init {"init": "<jwt>"}` → `{reboot_required?,
   instanceid, token, csr?, genuine}` returned as a typed, caller-owned
   `ehem_init_info` (freed by `ehem_init_info_free`). The returned bearer
   (`sub="U"`, scope `system:config`) is placed in the context's token
   cache so a follow-up config write needs no login.

**`cfg`** (fw v1.2.2 `api_auth.c:495-700`; completeness mask `0x3FFF`
at `:705`): **13 fields are mandatory** — `user`, `masterkey`,
`userkey`, `hostname`, `ip` (`A.B.C.D/prefix`), `origin`,
`storage_mode`, `storage_disk0size`, `trusted_ts`, `trusted_backend`,
`allow_keysearch`, `dnsd`, `email`; `gen_csr` (default false) and `ctx`
(default 0) are optional and outside the mask — the doc's "all 14 bits"
counts the firmware's final "all done" bit, not `gen_csr`. The SDK
sends every field the caller sets and the Manager's defaults for the
rest (`dnsd` false, `origin` "*", `ctx` 0, the trust booleans true).
Field validation is that of `POST /api/system/config`; a rejected field
is a 400 with no partial write.

**Preconditions**, in firmware order: RTC set (else 403 — the init
endpoints require it regardless of `trusted_ts`; the SDK does NOT
auto-check-in here, the caller or tool runs REQ-SYS-003 first), device
not initialised (else 406), `fls_state == 0` (else 409); the POST adds
401 on JWT/`jti` failure. Errors map per REQ-API-003 with
`ehem_last_error` detail (406 → `EHEM_ERR_DEVICE`, detail "already
initialised").

Works over an `http://` device URL by design — a wiped device has no
TLS material. The master secret, both private keys and the shared
secret are zeroized after use; the master secret is never retained.

**Rationale:** the Manager is the authoritative auth-flow reference
(user decision 2026-07-15) and its init flow is `build.js` `initFinal`:
PBKDF2 user key, master key from a BIP39 seed. The SDK takes the 32
master bytes and leaves mnemonic handling to the caller or tool
(REQ-TOOL-021). Deliberately unbound at 1.0 (no HEM-SDK-1..9 consumer
need); moved into M10 on 2026-08-07.

**Acceptance criteria:**
- [x] Unit (fake transport): GET → derive → POST sequence; the posted
      JWT decodes to the claim set above (`iss` = master public key,
      `aud` = `spk`, `exp` = challenge exp) with all 13 mandatory `cfg`
      fields and the Manager defaults; the tag verifies against
      ECDH(master, spk) recomputed by the test; the returned token is
      cached for scope `system:config` (next config write sends it
      without a login round-trip). *(STEP-M10-020, 2026-10-08:
      tests/unit/test_init.c test_init_full + _defaults_and_optional.)*
- [x] Unit: 403 / 406 / 409 / 401 / 400 mapped with distinguishable
      detail; ASan/LSan clean; secret buffers zeroized. *(test_init.c
      test_init_device_errors / _arg_validation. Recorded deviation from
      the plain REQ-API-003 table: the challenge's 403 means "RTC not
      set" — a device-state precondition, not an authorisation failure —
      so it is reported as `EHEM_ERR_DEVICE` with that explanation, like
      406/409; header documents it. Rev 2.)*
- [x] RESOLVED (STEP-M10-020, 2026-10-08) — fixture source: SDK
      self-consistency for the init JWT (the test rebuilds both personas
      from the fixture inputs and recomputes ECDH(master, spk) + the
      HMAC tag); the Manager's init JWT cannot be captured without
      initialising a device and the python client has no init. The
      master-secret side IS pinned to the Manager's own JavaScript
      (REQ-AUTH-012 vectors), and the login-side eJWT builder the init
      JWT reuses stays byte-pinned to the python fixture (test_ejwt).
- [ ] **Attended-only** (REQ-TEST-007): no live CTest. Evidence to
      record here: date, device, the init result (`instanceid`,
      `reboot_required`), then a passphrase login (`sub="U"`) and
      `hem-tool status` succeeding on the initialised device.
- [x] The public header documents the preconditions (run check-in
      first), the `http://` usage, that `masterkey` cannot be rotated
      later (wipe + re-init only), and that `ehem_tls_recover`
      (REQ-SYS-013) restores HTTPS afterwards. *(include/ehem/auth.h,
      STEP-M10-020, 2026-10-08.)*
