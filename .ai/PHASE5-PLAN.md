# Phase 5: Full API Coverage — Detailed Implementation Plan

## Current state (after MVP + phone auth)

### Implemented

| Module | Endpoints | Functions |
|---|---|---|
| System | `GET /api/system/version` | `hem_system_version` |
| System | `GET /api/system/status` | `hem_system_status` |
| System | `GET+POST /api/system/checkin` | `hem_system_checkin` |
| System | `GET /api/system/config` | `hem_system_config` |
| Auth | `GET+POST /api/auth/token` | `hem_auth_login`, `hem_auth_ensure` |
| Auth (ext) | `POST /api/auth/ext/init`, `ext/validate` | `hem_auth_ext_pair` |
| Auth (ext) | `POST /api/auth/ext/request`, `ext/token` | `hem_auth_ext_login` |
| Key mgmt | `POST /api/keymgmt/create` | `hem_key_create` |
| Key mgmt | `DELETE /api/keymgmt/delete/{kid}` | `hem_key_delete` |
| Key mgmt | `GET /api/keymgmt/list/{offset}/{limit}` | `hem_key_list` |
| Key mgmt | `GET /api/keymgmt/get/{kid}` | `hem_key_get` |
| Crypto | `POST /api/crypto/cipher/encrypt` | `hem_encrypt` |
| Crypto | `POST /api/crypto/cipher/decrypt` | `hem_decrypt` |

### Not yet implemented

| Module | Endpoints | Count |
|---|---|---|
| System | config POST, reboot, shutdown, selftest, attestation, provisioning | 6 |
| Auth | `POST /api/auth/init` (device init) | 1 |
| Key mgmt | derive, import, update, search | 4 |
| Crypto | HMAC hash/verify, ExDSA sign/verify, ECDH, wrap/unwrap | 6 |
| PQC | ML-KEM encaps/decaps, ML-DSA sign/verify | 4 |
| Audit log | key, list, download | 3 |
| Storage | unlock, lock | 2 |
| Firmware | usbmode, upload_fw, check_fw, install_fw, upload_ui, check_ui, install_ui | 7 |

Total: **33 new endpoints** to implement.

---

## Testing strategy

### Framework: CMocka

Recommended framework: **[CMocka](https://cmocka.org/)** (C unit testing with mocking).

**Why CMocka:**
- Pure C, no C++ dependency — matches the project's C11 codebase
- Built-in mock/stub support — essential for testing without a live device
- CMake integration via `find_package(cmocka)` or vendored
- TAP output format for CI integration
- Widely available on Linux (`apt install libcmocka-dev`)
- Permissive license (Apache 2.0)

**Alternatives considered:**
| Framework | Verdict |
|---|---|
| Unity | Good for embedded, but no built-in mocking (needs CMock codegen) |
| Check | Heavier, fork-based isolation adds complexity |
| CUnit | Older, less active, weaker mock support |
| Greatest | Header-only, nice, but no mocking |

### Test architecture

```
test/
├── hem_test.c                  # Existing MVP integration test (requires live device)
├── unit/                       # Unit tests (no device required)
│   ├── test_json.c             # JSON helpers, base64 encode/decode
│   ├── test_auth_ejwt.c        # eJWT construction with known test vectors
│   └── test_types.c            # Struct sizing, enum values
├── integration/                # Integration tests (requires live device)
│   ├── test_system.c           # System endpoints (read-only)
│   ├── test_keymgmt.c          # Key create/list/get/update/search/delete
│   ├── test_crypto_aes.c       # AES encrypt/decrypt/wrap/unwrap
│   ├── test_crypto_hmac.c      # HMAC hash/verify
│   ├── test_crypto_ecdh.c      # ECDH key agreement
│   ├── test_crypto_exdsa.c     # Digital signatures (ECDSA/EdDSA)
│   ├── test_pqc.c              # Post-quantum ML-KEM, ML-DSA
│   ├── test_logger.c           # Audit log (read-only)
│   └── test_auth_ext.c         # Phone app auth (interactive)
└── destructive/                # Destructive tests (SEPARATE — changes device state permanently)
    ├── test_config_write.c     # POST /api/system/config (changes device name, etc.)
    ├── test_reboot.c           # GET /api/system/reboot (reboots device)
    ├── test_shutdown.c         # GET /api/system/shutdown (PPA only, powers off)
    ├── test_storage.c          # Storage unlock/lock (PPA only, changes disk state)
    ├── test_firmware.c         # Firmware upload/check/install (overwrites firmware!)
    └── test_device_init.c      # POST /api/auth/init (factory provisioning, one-time)
```

### Test categories

| Category | Device needed | Safe to run | When to run |
|---|---|---|---|
| **unit/** | No | Always | Every build, CI |
| **integration/** | Yes | Yes — creates only temp keys, cleans up | Manual or CI with device |
| **destructive/** | Yes | **No** — permanent changes | Manual only, on dedicated test device |

### CMake test targets

```cmake
# Unit tests — always safe
add_test(NAME unit_tests COMMAND test_unit)

# Integration tests — needs HEM_TEST_URL and HEM_TEST_PASS env vars
add_test(NAME integration_tests COMMAND test_integration)
set_tests_properties(integration_tests PROPERTIES
    ENVIRONMENT "HEM_TEST_URL=https://my.ence.do;HEM_TEST_PASS=secret"
    LABELS "integration")

# Destructive tests — never run automatically
add_test(NAME destructive_tests COMMAND test_destructive)
set_tests_properties(destructive_tests PROPERTIES
    LABELS "destructive"
    DISABLED TRUE)
```

Run with: `ctest --label-regex integration` or `ctest --label-regex destructive` (explicitly).

---

## Implementation steps

Each step below implements one endpoint (or a tightly coupled pair). Steps are grouped by module and ordered so that earlier steps don't depend on later ones.

---

### Step 1 — System: POST /api/system/config (write)

**Endpoint:** `POST /api/system/config`
**Scope:** `system:config` | **Auth:** Yes (User, Master, External)

**Function signature:**
```c
hem_error_t hem_system_config_set(hem_ctx_t *ctx, const char *user_name,
                                   const char *tls_json);
```

**Implementation notes:**
- Only sends fields that are non-NULL
- `wipeout` intentionally NOT exposed in public API (too dangerous; add a separate `hem_system_wipeout()` later if needed)
- Returns the device's `updated` and `reboot_required` booleans

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `destructive/test_config_write.c`
- Set user name to `"test-user-<timestamp>"`, verify via `hem_system_config()`, restore original
- **Destructive** because it changes persistent device config

---

### Step 2 — System: GET /api/system/reboot

**Endpoint:** `GET /api/system/reboot`
**Scope:** any valid scope | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_system_reboot(hem_ctx_t *ctx);
```

**Implementation notes:**
- Simple authenticated GET, verify `{"status":"OK"}`
- Device will go offline for ~10-30 seconds after this call

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `destructive/test_reboot.c`
- Call reboot, wait, verify device comes back with `hem_system_status()`
- **Destructive** — device goes offline, all existing sessions/tokens are lost

---

### Step 3 — System: GET /api/system/shutdown

**Endpoint:** `GET /api/system/shutdown`
**Scope:** any valid scope | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_system_shutdown(hem_ctx_t *ctx);
```

**Implementation notes:**
- PPA only; returns 404 on EPA
- After this call the device is powered off — no further communication possible

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `destructive/test_shutdown.c`
- PPA only, manual verification that device powers off
- **Destructive** — physical intervention required to restart

---

### Step 4 — System: GET /api/system/selftest

**Endpoint:** `GET /api/system/selftest`
**Scope:** any valid scope | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_system_selftest(hem_ctx_t *ctx, int *fls_state_out);
```

**Implementation notes:**
- Poll until `kat_busy` is absent (up to 240 seconds)
- Return final `fls_state` value
- Use a polling loop with 2-second intervals, max 120 iterations

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `integration/test_system.c`
- Run selftest, verify `fls_state == 0`
- Safe — read-only (KAT doesn't alter device state)
- May take up to 4 minutes

---

### Step 5 — System: GET /api/system/config/attestation

**Endpoint:** `GET /api/system/config/attestation`
**Scope:** any valid | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_system_attestation(hem_ctx_t *ctx, char *genuine_out, size_t genuine_size);
```

**Implementation notes:**
- PPA only, returns 404 on EPA, 409 if device in FLS
- Returns opaque `genuine` blob

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `integration/test_system.c`
- Call on PPA, verify non-empty genuine string
- Call on EPA, verify HEM_ERR_HTTP_STATUS (404)

---

### Step 6 — Key management: POST /api/keymgmt/derive

**Endpoint:** `POST /api/keymgmt/derive`
**Scope:** `keymgmt:gen` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_key_derive(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            const char *ecdh_kid,
                            const char *peer_pubkey_b64,
                            char       *kid_out,
                            size_t      kid_size);
```

**Implementation notes:**
- Requires an existing ECDH-capable key (`ecdh_kid`)
- `peer_pubkey_b64` is the external peer's public key in standard base64
- ML-KEM/ML-DSA keys cannot be derived
- Returns 406 if shared secret too small for requested type

**Header:** `hem_keymgmt.h`
**Source:** `hem_keymgmt.c`

**Test:** `integration/test_keymgmt.c`
- Create a CURVE25519 key, derive an AES256 key from it with a test peer pubkey
- Verify derived KID is returned
- Clean up both keys

---

### Step 7 — Key management: POST /api/keymgmt/import

**Endpoint:** `POST /api/keymgmt/import`
**Scope:** `keymgmt:imp` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_key_import(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            const char *pubkey_b64,
                            const char *mode,
                            char       *kid_out,
                            size_t      kid_size);
```

**Implementation notes:**
- Imports only public keys (no private material crosses the API boundary)
- `mode` is optional: `"ECDH"`, `"ExDSA"`, or `"ECDH,ExDSA"`

**Header:** `hem_keymgmt.h`
**Source:** `hem_keymgmt.c`

**Test:** `integration/test_keymgmt.c`
- Generate a CURVE25519 keypair locally (OpenSSL), import the public key
- Verify KID is returned, key appears in list
- Delete the imported key

---

### Step 8 — Key management: POST /api/keymgmt/update

**Endpoint:** `POST /api/keymgmt/update`
**Scope:** `keymgmt:upd` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_key_update(hem_ctx_t  *ctx,
                            const char *kid,
                            const char *new_label,
                            const char *new_descr_b64);
```

**Implementation notes:**
- Either `new_label` or `new_descr_b64` can be NULL (at least one must be non-NULL)
- `new_descr_b64` is base64-encoded binary description

**Header:** `hem_keymgmt.h`
**Source:** `hem_keymgmt.c`

**Test:** `integration/test_keymgmt.c`
- Create a key, update label, verify via `hem_key_get()`
- Clean up

---

### Step 9 — Key management: POST /api/keymgmt/search

**Endpoint:** `POST /api/keymgmt/search`
**Scope:** `keymgmt:search` (or unauthenticated if `allow_keysearch` enabled) | **Auth:** Optional

**Function signature:**
```c
hem_error_t hem_key_search(hem_ctx_t      *ctx,
                            const char     *descr_b64,
                            int             offset,
                            int             limit,
                            hem_key_info_t *list,
                            int             list_cap,
                            int            *total,
                            int            *listed);
```

**Implementation notes:**
- `descr_b64` is a base64-encoded search pattern; prefix with `^` for starts-with
- Response format matches `/api/keymgmt/list`
- Returns 404 if no keys match

**Header:** `hem_keymgmt.h`
**Source:** `hem_keymgmt.c`

**Test:** `integration/test_keymgmt.c`
- Create a key with a unique `descr`, search for it, verify found
- Search with non-matching pattern, verify 404 / empty result
- Clean up

---

### Step 10 — Crypto: HMAC hash

**Endpoint:** `POST /api/crypto/hmac/hash`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_hmac_hash(hem_ctx_t     *ctx,
                           const char    *kid,
                           const char    *alg,
                           const uint8_t *msg,
                           size_t         msg_len,
                           uint8_t       *mac_out,
                           size_t         mac_size,
                           size_t        *mac_len);
```

**Implementation notes:**
- `alg` only needed when using ECDH-derived HMAC; can be NULL for direct HMAC keys
- `msg` max 2048 bytes raw
- ECDH-derived HMAC (`ext_kid`/`pubkey` params) deferred to a later step

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_hmac.c`
- Create a SHA2-256 HMAC key
- Hash a test message, verify non-empty MAC returned
- Hash same message again, verify MAC is identical (deterministic)
- Clean up key

---

### Step 11 — Crypto: HMAC verify

**Endpoint:** `POST /api/crypto/hmac/verify`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_hmac_verify(hem_ctx_t     *ctx,
                             const char    *kid,
                             const char    *alg,
                             const uint8_t *msg,
                             size_t         msg_len,
                             const uint8_t *mac,
                             size_t         mac_len);
```

**Implementation notes:**
- Returns `HEM_OK` if MAC is valid
- Returns `HEM_ERR_AUTH` (401/403) if verification fails
- Use same scope token as the hash call

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_hmac.c`
- Hash a message, then verify the returned MAC — expect HEM_OK
- Verify with a corrupted MAC — expect failure
- Clean up key

---

### Step 12 — Crypto: ExDSA sign

**Endpoint:** `POST /api/crypto/exdsa/sign`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_sign(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *alg,
                      const uint8_t *msg,
                      size_t         msg_len,
                      const uint8_t *sign_ctx,
                      size_t         sign_ctx_len,
                      uint8_t       *sig_out,
                      size_t         sig_size,
                      size_t        *sig_len);
```

**Implementation notes:**
- `alg` e.g. `"Ed25519"`, `"SHA256WithECDSA"`
- `sign_ctx` is the optional context for Ed25519ctx/Ed448 modes (NULL for most cases)
- Signature format varies by algorithm (raw for EdDSA, DER for ECDSA)

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_exdsa.c`
- Create an ED25519 key
- Sign a test message
- Verify the signature (step 13)
- Also test SECP256R1 with SHA256WithECDSA
- Clean up keys

---

### Step 13 — Crypto: ExDSA verify

**Endpoint:** `POST /api/crypto/exdsa/verify`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_verify(hem_ctx_t     *ctx,
                        const char    *kid,
                        const char    *alg,
                        const uint8_t *msg,
                        size_t         msg_len,
                        const uint8_t *sig,
                        size_t         sig_len,
                        const uint8_t *sign_ctx,
                        size_t         sign_ctx_len);
```

**Implementation notes:**
- Returns `HEM_OK` if signature is valid
- Returns error (401/403) if verification fails

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_exdsa.c` (same test file as step 12)
- Verify signature from step 12 — expect HEM_OK
- Verify with corrupted signature — expect failure

---

### Step 14 — Crypto: ECDH key agreement

**Endpoint:** `POST /api/crypto/ecdh`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_ecdh(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *peer_pubkey_b64,
                      const char    *peer_kid,
                      const char    *alg,
                      uint8_t       *secret_out,
                      size_t         secret_size,
                      size_t        *secret_len);
```

**Implementation notes:**
- Either `peer_pubkey_b64` (external key) or `peer_kid` (imported key) must be non-NULL
- `alg` is the hash algorithm for output (NULL for raw ECDH)
- Supported key types: CURVE25519, CURVE448, SECP256R1/384R1/521R1/256K1

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_ecdh.c`
- Create two CURVE25519 keys on device
- Get pubkey of key B via `hem_key_get()`
- Perform ECDH(A, pubkey_B) and ECDH(B, pubkey_A)
- Verify both shared secrets are identical
- Clean up keys

---

### Step 15 — Crypto: AES key wrap

**Endpoint:** `POST /api/crypto/cipher/wrap`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_key_wrap(hem_ctx_t     *ctx,
                          const char    *kid,
                          const char    *alg,
                          const uint8_t *key_material,
                          size_t         key_len,
                          uint8_t       *wrapped_out,
                          size_t         wrapped_size,
                          size_t        *wrapped_len);
```

**Implementation notes:**
- `alg`: `"AES128"`, `"AES192"`, or `"AES256"`
- `key_material` must be a multiple of 8 bytes, minimum 16 bytes
- Output is 8 bytes larger than input (RFC 3394 overhead)

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_aes.c`
- Create an AES256 key for wrapping
- Wrap 32 bytes of test key material
- Unwrap (step 16) and verify original key material is recovered
- Clean up

---

### Step 16 — Crypto: AES key unwrap

**Endpoint:** `POST /api/crypto/cipher/unwrap`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_key_unwrap(hem_ctx_t     *ctx,
                            const char    *kid,
                            const char    *alg,
                            const uint8_t *wrapped,
                            size_t         wrapped_len,
                            uint8_t       *key_out,
                            size_t         key_size,
                            size_t        *key_len);
```

**Header:** `hem_crypto.h`
**Source:** `hem_crypto.c`

**Test:** `integration/test_crypto_aes.c` (same test as step 15)
- Unwrap the output from step 15, compare with original key material

---

### Step 17 — PQC: ML-KEM encaps

**Endpoint:** `POST /api/crypto/pqc/mlkem/encaps`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_mlkem_encaps(hem_ctx_t *ctx,
                              const char *kid,
                              uint8_t    *shared_secret_out,
                              size_t      ss_size,
                              size_t     *ss_len,
                              uint8_t    *ciphertext_out,
                              size_t      ct_size,
                              size_t     *ct_len);
```

**Implementation notes:**
- Key types: MLKEM512, MLKEM768, MLKEM1024
- Returns both shared secret and ciphertext
- Shared secret size: 32 bytes for all ML-KEM variants

**Header:** add new `hem_pqc.h`
**Source:** add new `hem_pqc.c`

**Test:** `integration/test_pqc.c`
- Create MLKEM768 key
- Encaps → get shared secret + ciphertext
- Decaps (step 18) with same ciphertext → verify shared secrets match
- Clean up

---

### Step 18 — PQC: ML-KEM decaps

**Endpoint:** `POST /api/crypto/pqc/mlkem/decaps`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_mlkem_decaps(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *ciphertext,
                              size_t         ct_len,
                              uint8_t       *shared_secret_out,
                              size_t         ss_size,
                              size_t        *ss_len);
```

**Header:** `hem_pqc.h`
**Source:** `hem_pqc.c`

**Test:** `integration/test_pqc.c` (same test as step 17)

---

### Step 19 — PQC: ML-DSA sign

**Endpoint:** `POST /api/crypto/pqc/mldsa/sign`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_mldsa_sign(hem_ctx_t     *ctx,
                            const char    *kid,
                            const uint8_t *msg,
                            size_t         msg_len,
                            uint8_t       *sig_out,
                            size_t         sig_size,
                            size_t        *sig_len);
```

**Implementation notes:**
- Key types: MLDSA44, MLDSA65, MLDSA87
- Signature sizes: 2420 (MLDSA44), 3309 (MLDSA65), 4627 (MLDSA87)

**Header:** `hem_pqc.h`
**Source:** `hem_pqc.c`

**Test:** `integration/test_pqc.c`
- Create MLDSA65 key
- Sign test message
- Verify (step 20)
- Clean up

---

### Step 20 — PQC: ML-DSA verify

**Endpoint:** `POST /api/crypto/pqc/mldsa/verify`
**Scope:** `keymgmt:use:<kid>` | **Auth:** Yes | **TLS:** Required

**Function signature:**
```c
hem_error_t hem_mldsa_verify(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *msg,
                              size_t         msg_len,
                              const uint8_t *sig,
                              size_t         sig_len);
```

**Header:** `hem_pqc.h`
**Source:** `hem_pqc.c`

**Test:** `integration/test_pqc.c` (same test as step 19)

---

### Step 21 — Audit log: GET /api/logger/key

**Endpoint:** `GET /api/logger/key`
**Scope:** `logger:get` | **Auth:** Yes

**Function signature:**
```c
typedef struct {
    char key_b64[128];           /* ED25519 public key (base64) */
    char nonce_b64[128];         /* Random nonce (base64) */
    char nonce_signed_b64[256];  /* ED25519 signature of nonce (base64) */
} hem_logger_key_t;

hem_error_t hem_logger_key(hem_ctx_t *ctx, hem_logger_key_t *out);
```

**Header:** add new `hem_logger.h`
**Source:** add new `hem_logger.c`

**Test:** `integration/test_logger.c`
- Retrieve logger key, verify all three fields are non-empty
- Optionally verify signature locally (Ed25519 verify nonce with key)

---

### Step 22 — Audit log: GET /api/logger/list/{offset}

**Endpoint:** `GET /api/logger/list/{offset}`
**Scope:** `logger:get` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_logger_list(hem_ctx_t *ctx, int offset,
                             int *ids_out, int ids_cap, int *count);
```

**Implementation notes:**
- PPA only; returns 404 on EPA
- Returns array of integer log file IDs

**Header:** `hem_logger.h`
**Source:** `hem_logger.c`

**Test:** `integration/test_logger.c`
- List log files, verify at least one exists
- Test on EPA, verify 404 handling

---

### Step 23 — Audit log: GET /api/logger/{id}

**Endpoint:** `GET /api/logger/{id}`
**Scope:** `logger:get` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_logger_download(hem_ctx_t *ctx, int log_id,
                                 char *buf, size_t buf_size, size_t *out_len);
```

**Implementation notes:**
- PPA only; returns plain text (not JSON)
- Response may be large; use generous buffer or streaming approach
- Pipe-delimited format, 7 fields per entry

**Header:** `hem_logger.h`
**Source:** `hem_logger.c`

**Test:** `integration/test_logger.c`
- Download first log from the list, verify non-empty and pipe-delimited format

---

### Step 24 — Storage: GET /api/storage/unlock

**Endpoint:** `GET /api/storage/unlock`
**Scope:** `storage:disk0:rw` or `storage:disk0:ro` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_storage_unlock(hem_ctx_t *ctx, const char *scope);
```

**Implementation notes:**
- PPA only; returns 404 on EPA
- The `scope` parameter determines which disk and mode: `"storage:disk0:rw"`, `"storage:disk0:ro"`, etc.
- The function authenticates with the given scope, then calls GET /api/storage/unlock

**Header:** add new `hem_storage.h`
**Source:** add new `hem_storage.c`

**Test:** `destructive/test_storage.c`
- **Destructive** — changes disk lock state
- Unlock disk0, verify via status, lock again

---

### Step 25 — Storage: GET /api/storage/lock

**Endpoint:** `GET /api/storage/lock`
**Scope:** `storage:disk0:rw` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_storage_lock(hem_ctx_t *ctx, const char *scope);
```

**Header:** `hem_storage.h`
**Source:** `hem_storage.c`

**Test:** `destructive/test_storage.c` (same as step 24)

---

### Step 26 — System: POST /api/system/config/provisioning

**Endpoint:** `POST /api/system/config/provisioning`
**Scope:** `system:config` | **Auth:** Yes

**Function signature:**
```c
hem_error_t hem_system_provision(hem_ctx_t  *ctx,
                                  const char *crt,
                                  const char *genuine);
```

**Implementation notes:**
- PPA only, one-time operation
- Returns 403 if already provisioned
- Requires attestation data from `hem_system_attestation()` and certificate from backend

**Header:** `hem_system.h`
**Source:** `hem_system.c`

**Test:** `destructive/test_device_init.c`
- **Destructive** — cannot be undone without factory reset
- Test only on fresh/unprovisioned device

---

### Step 27 — Auth: POST /api/auth/init (device initialization)

**Endpoint:** `GET + POST /api/auth/init`
**Auth:** No (device must be uninitialized)

**Function signature:**
```c
typedef struct {
    char instanceid[128];
    char token[2048];
    char csr[4096];
    char genuine[2048];
} hem_init_result_t;

hem_error_t hem_auth_device_init(hem_ctx_t             *ctx,
                                  const char            *master_pass,
                                  const char            *user_pass,
                                  const char            *user_name,
                                  const char            *email,
                                  const char            *hostname,
                                  const hem_init_config_t *opts,
                                  hem_init_result_t     *result);
```

**Implementation notes:**
- Two-phase: GET challenge, construct eJWT with `cfg` payload, POST
- This is the most complex single endpoint — constructs an eJWT with embedded config
- Only works on uninitialized devices (returns 406 otherwise)
- **Irreversible** without factory reset

**Header:** `hem_system.h` (or separate `hem_init.h`)
**Source:** `hem_auth.c` (reuses eJWT machinery)

**Test:** `destructive/test_device_init.c`
- **Destructive** — initializes the device permanently
- Only run on factory-fresh devices

---

### Steps 28-33 — Firmware upgrade endpoints

**Endpoints:**
| Step | Endpoint | Function |
|---|---|---|
| 28 | `GET /api/system/upgrade/usbmode` | `hem_upgrade_usbmode` |
| 29 | `POST /api/system/upgrade/upload_fw` | `hem_upgrade_upload_fw` |
| 30 | `GET /api/system/upgrade/check_fw` | `hem_upgrade_check_fw` |
| 31 | `GET /api/system/upgrade/install_fw` | `hem_upgrade_install_fw` |
| 32 | `POST /api/system/upgrade/upload_ui` | `hem_upgrade_upload_ui` |
| 33 | `GET /api/system/upgrade/install_ui` | `hem_upgrade_install_ui` |

**Scope:** `system:upgrade` for all | **Auth:** Yes

**Implementation notes:**
- `upload_fw` and `upload_ui` are binary uploads, NOT JSON — need a new `hem_http_post_binary()` transport function
- `check_fw` returns 202 while verifying — poll until 200
- `install_fw` causes device reboot
- `usbmode` is PPA only, puts device in DFU mode (USB serial)

**Header:** add new `hem_upgrade.h`
**Source:** add new `hem_upgrade.c`

**Test:** `destructive/test_firmware.c`
- **Destructive** — overwrites firmware/UI, reboots device
- Only run on dedicated test device with known-good firmware to restore
- Upload test firmware, check integrity, do NOT install unless explicitly intended

---

## Implementation order

---

## Section A — Start now

These steps have no blocking open questions. They can be implemented and tested
on the Ubuntu dev machine immediately.

> **Assumptions made for steps with minor OQs:**
> - Step 8 (`hem_key_update`): success is determined by HTTP 200 alone (OQ-6).
> - Step 9 (`hem_key_search`): 6-byte minimum refers to raw binary before base64; test
>   patterns will be at least 6 raw bytes (OQ-7).
> - Steps 11 & 13 (`hem_hmac_verify`, `hem_verify`): verify endpoints return an empty
>   body; success determined by HTTP 200 (OQ-12).
> - Step 23 (`hem_logger_download`): implement download as raw text; field names in
>   tests left as `TODO` until OQ-8 is resolved.
> - Steps 24-25 (`hem_storage_unlock/lock`): scope format is `storage:diskN:rw`; read-only
>   scope `storage:diskN:ro` assumed valid (OQ-11).
> - Step 30 (`hem_upgrade_check_fw`): poll every 2 seconds, timeout after 120 s (OQ-10).

| Priority | Steps | Topic |
|---|---|---|
| **P0** | Unit tests (`test_json.c`, `test_auth_ejwt.c`) | Testing infrastructure, no device needed |
| **P1** | 6-9 | Key mgmt: derive, import, update, search |
| **P1** | 10-13 | Crypto: HMAC hash/verify, ExDSA sign/verify |
| **P1** | 14 | Crypto: ECDH key agreement |
| **P2** | 15-16 | Crypto: AES wrap/unwrap |
| **P2** | 17-20 | PQC: ML-KEM encaps/decaps, ML-DSA sign/verify |
| **P2** | 4-5 | System: selftest, attestation |
| **P3** | 21-23 | Audit log: key, list, download |
| **P3** | 1-3 | System: config write, reboot, shutdown |
| **P3** | 24-25 | Storage: unlock, lock |
| **P4** | 26-27 | Provisioning + device init (one-time, irreversible) |
| **P4** | 28, 30, 31, 33 | Firmware: usbmode, check_fw, install_fw, install_ui |

---

## Section B — Needs clarification first

These steps are blocked by open questions in `OPEN-QUESTIONS.md` that must be
answered before implementation can begin. The OQ reference is noted for each.

### Steps 29 & 32 — Firmware binary upload (blocked by OQ-9)

`POST /api/system/upgrade/upload_fw` and `POST /api/system/upgrade/upload_ui`
require a new `hem_http_post_binary()` transport function, but the wire format
is unknown:

- Is it `multipart/form-data` or raw `application/octet-stream`?
- If multipart, what is the form field name?
- What is the maximum accepted file size?

**Unblock:** resolve OQ-9, then add `hem_http_post_binary()` and implement both
upload functions.

---

### `hem_auth_ext_pair` / `hem_auth_ext_login` stubs (blocked by OQ-1, OQ-2, OQ-3)

These functions exist from the Phase 4 MVP but are stubbed because the
notification broker URL returns HTTP 404.  Three separate questions must all
be answered:

- **OQ-1:** What is the correct broker URL? Does it include the device `eid`?
- **OQ-2:** What JSON does the broker return for pairing (`pid`/`reply`?) and for
  login (`authreply`?)? Does it block until the phone approves, or require polling?
- **OQ-3:** Does the caller generate the `epk` keypair, or does it come from the
  Encedo cloud? Is the broker response encrypted to `epk`?

**Unblock:** resolve OQ-1 through OQ-3, then complete both ext-auth functions
and write `test/integration/test_auth_ext.c`.

---

## New files summary

| File | Type | Contents |
|---|---|---|
| `include/hem/hem_pqc.h` | Header | ML-KEM encaps/decaps, ML-DSA sign/verify |
| `include/hem/hem_logger.h` | Header | Logger key, list, download |
| `include/hem/hem_storage.h` | Header | Storage unlock/lock |
| `include/hem/hem_upgrade.h` | Header | Firmware/UI upload/check/install |
| `src/hem_pqc.c` | Source | PQC implementations |
| `src/hem_logger.c` | Source | Logger implementations |
| `src/hem_storage.c` | Source | Storage implementations |
| `src/hem_upgrade.c` | Source | Firmware upgrade implementations |
| `test/unit/test_json.c` | Test | Base64, JSON helpers |
| `test/unit/test_auth_ejwt.c` | Test | eJWT construction with known vectors |
| `test/integration/test_system.c` | Test | System read-only endpoints |
| `test/integration/test_keymgmt.c` | Test | Key CRUD + derive/import/search |
| `test/integration/test_crypto_aes.c` | Test | AES encrypt/decrypt/wrap/unwrap |
| `test/integration/test_crypto_hmac.c` | Test | HMAC hash/verify |
| `test/integration/test_crypto_ecdh.c` | Test | ECDH key agreement |
| `test/integration/test_crypto_exdsa.c` | Test | ExDSA sign/verify |
| `test/integration/test_pqc.c` | Test | ML-KEM + ML-DSA roundtrips |
| `test/integration/test_logger.c` | Test | Audit log read-only |
| `test/integration/test_auth_ext.c` | Test | Phone app auth (interactive) |
| `test/destructive/test_config_write.c` | Test | Config POST |
| `test/destructive/test_reboot.c` | Test | Reboot |
| `test/destructive/test_shutdown.c` | Test | Shutdown (PPA) |
| `test/destructive/test_storage.c` | Test | Storage lock/unlock (PPA) |
| `test/destructive/test_firmware.c` | Test | Firmware upgrade |
| `test/destructive/test_device_init.c` | Test | Device init + provisioning |
