---
id: STEP-M10-020
title: "Device-init binding: ehem_device_init (auth/init challenge + master-signed cfg commit)"
milestone: M10
implements: ["REQ-AUTH-011", "REQ-AUTH-012"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "verifies: REQ-AUTH-011 — tests/unit/test_init.c (full sequence: minimal header, claims jti/aud/exp=challenge/iat/iss=master pub, 13 cfg fields + Manager defaults, tag recomputed from ECDH(master, spk), system:config bearer cached with zero login traffic; defaults/optional reply/NULL out; 403→DEVICE 'RTC', 406, 409, 400, 401→AUTH_FAILED with detail; malformed challenge/reply → PROTOCOL; arg validation with zero traffic)"
    - "verifies: REQ-AUTH-012 — tests/unit/test_bip39.c (3 Manager-code fixtures incl. the nibble shift proven against the un-shifted seed; whitespace canonicalisation; checksum/count/unknown/upper-case rejections with last-error reasons; encoder pinned to the BIP39 reference mnemonics; generate round-trip)"
  notes: >
    2026-10-08. SDK: ehem_device_init + ehem_init_params(_init) +
    ehem_init_info(_free) and ehem_mnemonic_generate/_free +
    ehem_master_secret_from_mnemonic in include/ehem/auth.h and
    src/proto_auth.c (implements: REQ-AUTH-011, REQ-AUTH-012). New internal
    module src/bip39.{h,c} (generate / validate / the Manager's
    substr(1,64) slice) over the vendored English wordlist
    src/vendor/bip39/ (generated from the Manager's wordlist_english_v1.js,
    MIT — Pavol Rusnak; VENDORED.md with source SHA-256). Shim additions:
    ehem_kdf_pbkdf2_sha512, ehem_sha256, ehem_random_bytes (wolfCrypt).
    ejwt.c: the compact-token tail factored into sign_compact(); new
    ehem_ejwt_sign(header, payload, key) for the master-signed init JWT
    (login eJWT fixture still byte-exact — test_ejwt green). json.c:
    ehem_json_add_bool. Fixtures: the three mnemonic vectors come from the
    Manager's OWN JavaScript (sjcl-bip39 + jsbip39 + wordlist under node
    24, scratchpad bip39_fixture.js, command recorded in the test); the
    init JWT is self-consistency-checked (REQ-AUTH-011 criterion resolved).
    Design choices recorded in the REQs: 403 on the challenge reported as
    EHEM_ERR_DEVICE "RTC not set" (not SCOPE_DENIED); no auto-check-in
    inside the binding; the returned bearer seeds the cache after an
    internal ehem_login(passphrase). NO live test (REQ-TEST-007). Verified:
    ./dev ci 43/43 on gcc AND clang (41 + test_bip39 + test_init); ./dev test
    asan clean; ./dev check (export baseline + public-header + docs gates)
    green; MinGW -fsyntax-only clean on proto_auth.c, bip39.c, ejwt.c,
    json.c, wordlist_english.c (crypto_shim.c needs wolfSSL headers the
    cross toolchain lacks — it has no ehem_ctx_fail calls, the trap the
    check exists for). REQ-AUTH-012 → verified (pure crypto, tests pass);
    REQ-AUTH-011 → implemented, attended run at the M10 gate.
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
- [x] Binding, params/info structs, free function, header docs
      (preconditions, http:// use, masterkey not rotatable, HTTPS
      restore via `ehem_tls_recover`); API-GUIDE updated.
- [x] Unit tests (new tests/unit/test_init.c): sequence, claim set, 13
      cfg fields + defaults, tag verifies, token cached for
      system:config; 403/406/409/401/400 mapped with detail; secrets
      zeroized; ASan/LSan clean. Plus tests/unit/test_bip39.c for the
      REQ-AUTH-012 helpers (Manager-code fixtures).
- [x] Fixture decision recorded in REQ-AUTH-011 (criterion resolved:
      self-consistency for the init JWT; Manager-code vectors for the
      master secret).
- [x] No live test added; `./dev ci` + gates green; `implements:
      REQ-AUTH-011, REQ-AUTH-012` tagged.
