# encedo-hem-c-api

**v0.1 — work in progress**

C client library (`libhem`) for the [Encedo HEM](https://encedo.com) (Hardware Encryption Module) REST API.
The library wraps the device endpoints and handles transport, authentication, and token management so
application code never has to deal with the eJWT protocol directly.

## What's implemented in v0.1

**System**
- `hem_system_version` — hardware/firmware version
- `hem_system_status` — device operational status
- `hem_system_checkin` — two-phase check-in (device ↔ Encedo backend)
- `hem_system_config` — device configuration

**Key management**
- `hem_key_create` — generate a key on the device
- `hem_key_delete` — delete a key
- `hem_key_list` — list keys (paginated)
- `hem_key_get` — get key metadata by ID

**Crypto**
- `hem_encrypt` — AES encrypt (GCM, CBC, ECB)
- `hem_decrypt` — AES decrypt

Authentication (eJWT) is handled automatically by all functions that require it.

**Phone app authentication (external authenticator)** *(partially implemented — see below)*
- `hem_auth_ext_pair` — one-time pairing of the Encedo phone app
- `hem_auth_ext_login` — authenticate using the paired phone app (no passphrase needed)

## Dependencies

| Library | Purpose |
|---|---|
| libcurl | HTTP/HTTPS transport |
| OpenSSL (libcrypto) | PBKDF2-SHA256, X25519 ECDH, HMAC-SHA256 |
| cJSON | JSON parsing (vendored in `third_party/cJSON/`) |

## Build

```sh
mkdir build && cd build
cmake ..
make
```

This produces `libhem.a` (static library) and `hem_test` (MVP test program).

## MVP test program

`hem_test` demonstrates a full end-to-end flow against a configured device:

1. Print device status
2. Perform check-in
3. Create an AES-256 key
4. Encrypt a random message
5. Decrypt the message and verify it matches
6. Delete the created key

## Phone app authentication

> **Status: partially implemented.** The device-side endpoints (`/api/auth/ext/*`) are
> mapped. The intermediate step — forwarding the device challenge to the Encedo
> notification broker so the phone receives a push notification — is not yet implemented
> because the broker URL and response format are not confirmed in the API documentation.
> Both `hem_auth_ext_pair` and `hem_auth_ext_login` currently return `HEM_ERR_CHECKIN`
> with a descriptive message. See `.ai/OPEN-QUESTIONS.md` for details.

### Intended pairing flow (one-time)

```c
char confirmation[512];
// epk_b64: your ephemeral X25519 public key (standard base64)
hem_error_t err = hem_auth_ext_pair(ctx, epk_b64, confirmation, sizeof(confirmation));
```

1. Registers the session key with the device (`POST /api/auth/ext/init`)
2. Forwards the pairing challenge to the Encedo notification broker *(URL TBD)*
3. Waits for the user to approve on the phone app
4. Confirms pairing on the device (`POST /api/auth/ext/validate`)

### Intended authentication flow

```c
// Use a fresh ephemeral keypair for each login
hem_error_t err = hem_auth_ext_login(ctx, epk_b64, "keymgmt:gen");
```

1. Requests an auth challenge from the device (`POST /api/auth/ext/request`)
2. Forwards the challenge to the notification broker *(URL TBD)*
3. Exchanges the phone's signed reply for a JWT token (`POST /api/auth/ext/token`)
4. Caches the token in the context for all subsequent API calls

## Repository layout

```
include/hem/        Public headers (include hem/hem.h)
src/                Library implementation
test/               MVP test program
third_party/cJSON/  Vendored cJSON
scripts/            build.sh, test.sh helpers
```
