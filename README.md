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

## Repository layout

```
include/hem/        Public headers (include hem/hem.h)
src/                Library implementation
test/               MVP test program
third_party/cJSON/  Vendored cJSON
scripts/            build.sh, test.sh helpers
```
