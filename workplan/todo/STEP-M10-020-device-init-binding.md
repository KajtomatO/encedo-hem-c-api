---
id: STEP-M10-020
title: "Device-init binding: ehem_device_init (auth/init challenge + master-signed cfg commit)"
milestone: M10
implements: ["REQ-AUTH-011", "REQ-AUTH-012"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_device_init(ctx, const ehem_init_params *, ehem_init_info
**out)` + `ehem_init_info_free` in `include/ehem/auth.h` /
`src/proto_auth.c`: `GET /api/auth/init` → PBKDF2 user key (REQ-AUTH-001
derivation, `crypto_shim`) + caller's 32-byte master secret → X25519
pairs → init JWT **signed with the master key** (`ejwt.c`, minimal
header `{"ecdh":"x25519"}`, `exp` = challenge exp, `iss` = master pub)
carrying the 13 mandatory `cfg` fields with the Manager's defaults →
`POST /api/auth/init` → typed `{reboot_required, instanceid, token, csr,
genuine}`; the returned bearer seeds the token cache for
`system:config`. Works over `http://`. Plus the two Manager-compatible
master-secret helpers of REQ-AUTH-012 — `ehem_mnemonic_generate` and
`ehem_master_secret_from_mnemonic` (BIP39: PBKDF2-HMAC-SHA512, 2048
rounds, salt "mnemonic", NFKD; then the Manager's `substr(1, 64)` nibble
shift) — with the English wordlist vendored into the library (license
confirmed from the Manager's `jsbip39_v1.js` header first).

**Notes:** Reference = encedo-manager `assets/build.js` `initFinal`
(b33c236, lines 655-770) — never `encedo.js`. `ehem_init_params` is a
public input struct: follow the `ehem_options` growable-struct
discipline (`abi_size` stamped by an `_init()` helper, append-only;
REQ-API-008). cfg mask facts: fw `api_auth.c:495-705`, `0x3FFF`;
`gen_csr`/`ctx` optional. **Resolve at step start (REQ-AUTH-011 open
criterion):** the fixture source for the byte-level test — a JS snippet
over `build.js`'s `pbkdf2KeyDerive`/`jwt_generate_hs256` with fixed
inputs, or SDK self-consistency (test recomputes ECDH + tag). No
auto-check-in inside the binding (403 = RTC unset is the caller's cue).
Attended-only (REQ-TEST-007): NO live test file; the attended init runs
at STEP-M10-070. `docs/API-GUIDE.md` must index every new symbol.

**Definition of done**
- [ ] Binding, params/info structs, free function, header docs
      (preconditions, http:// use, masterkey not rotatable, HTTPS
      restore via `ehem_tls_recover`); API-GUIDE updated.
- [ ] Unit tests (new tests/unit/test_init.c or test_auth.c): sequence,
      claim set, 13 cfg fields + defaults, tag verifies, token cached
      for system:config; 403/406/409/401/400 mapped with detail; secrets
      zeroized; ASan/LSan clean.
- [ ] Fixture decision recorded in REQ-AUTH-011 (criterion resolved).
- [ ] No live test added; `./dev ci` + gates green; `implements:
      REQ-AUTH-011` tagged.
