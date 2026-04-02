# Encedo HEM REST API Design

## 1. Overview

Encedo HEM (Hardware Encryption Module) is a general-purpose HSM that exposes all functionality through a REST API over HTTP(S). It exists in two form factors:

- **PPA** (Personal Privacy Assistant) -- USB-format device, default domain `my.ence.do`
- **EPA** (Enterprise Privacy Appliance) -- 19" rack-mount appliance, custom domain `*.cloud.ence.do`

The device acts as a cryptographic key vault and co-processor: keys never leave the HSM; all crypto operations execute "at the key" via API calls.

### Transport

| Property | Value |
|---|---|
| Protocol | HTTP and HTTPS (TLS 1.3) |
| Content-Type | `application/json` (all request/response bodies) |
| Max POST body | 7300 bytes (5 x MTU) |
| Authentication | JWT Bearer token in `Authorization` header |
| Base URL | `https://{hostname}/api/` |

### Authentication Model

Access control is based on **JWT tokens** with two dimensions:

1. **Role** (encoded in `sub` claim):
   - `U` -- local user (passphrase-based)
   - `M` -- master/admin (passphrase-based)
   - `E` -- remote user (external authenticator)

2. **Scope** (encoded in `scope` claim) -- grants access to specific operations or keys. Examples: `system:config`, `keymgmt:gen`, `keymgmt:use:<KID>`, `logger:get`, `storage:disk0:rw`.

### eJWT Authentication Protocol

Local authentication uses a custom **eJWT** (Encedo JWT) scheme:
- Header: `{"ecdh":"x25519","alg":"HS256","typ":"JWT"}`
- Signing key derivation: `PBKDF2-SHA256(passphrase, eid, 600000 iterations)` -> X25519 seed -> `X25519(seed, device_spk)` -> HMAC-SHA256 key
- The eJWT is sent to `POST /api/auth/token` as `{"auth": "<ejwt_string>"}`

### Common HTTP Status Codes

| Code | Meaning |
|---|---|
| 200 | Success |
| 400 | Bad request (malformed JSON, invalid argument) |
| 401 | Missing or invalid JWT token |
| 403 | Incorrect access scope or role not permitted |
| 404 | Resource not found / endpoint not available on this configuration |
| 406 | Not acceptable (e.g., device already initialized) |
| 409 | Conflict (device in failure state -- FLS active) |
| 413 | Request body too large |
| 418 | TLS connection required |

---

## 2. API Endpoint Reference

### 2.1 System

#### GET /api/system/version

Returns hardware and firmware version information.

- **Auth required**: No
- **Scopes**: None

**Response 200:**
```json
{
  "hwv": "PPA-1.0",
  "blv": "1.0.0",
  "fwv": "1.2.2",
  "fws": "a1b2c3...",
  "uis": "d4e5f6..."
}
```

| Field | Type | Description |
|---|---|---|
| `hwv` | string | Hardware version (contains "EPA" for Enterprise model) |
| `blv` | string | Bootloader version |
| `fwv` | string | Firmware version (suffix "-DIAG" indicates diagnostic build) |
| `fws` | string | Firmware signature (unique per binary) |
| `uis` | string | UI/dashboard signature (PPA only) |

---

#### GET /api/system/status

Returns device operational status.

- **Auth required**: No
- **Scopes**: None

**Response 200:**
```json
{
  "fls_state": 0,
  "ts": 1659436962,
  "hostname": "my.ence.do",
  "https": true,
  "inited": null,
  "format": "done",
  "uptime": 3600,
  "temp": 42,
  "storage": ["disk0:rw", "disk1:-"]
}
```

| Field | Type | Description |
|---|---|---|
| `fls_state` | integer | Failure state bitmask (0 = no errors). Bits: entropy failure, integrity failure, temp out-of-range |
| `ts` | integer | Current RTC timestamp (absent if RTC not set) |
| `hostname` | string | Configured FQDN |
| `https` | boolean | Whether TLS is operational |
| `inited` | null/absent | Present (as key) only if device is **not** initialized; absent when initialized |
| `format` | string | Storage format status: `"done"` when complete (PPA only, transient) |
| `storage` | array | Disk status array (PPA only). Format: `"diskN:mode"` where mode is `rw`, `ro`, or `-` (locked) |

---

#### GET /api/system/checkin

Initiates the check-in procedure (phase 1). Returns a challenge to forward to the Encedo backend.

- **Auth required**: No
- **Scopes**: None

**Response 200:**
```json
{
  "check": "<opaque_challenge_data>"
}
```

The entire response object must be forwarded to `POST https://api.encedo.com/checkin`.

---

#### POST /api/system/checkin

Completes the check-in procedure (phase 2). Accepts the response from the Encedo backend.

- **Auth required**: No
- **Scopes**: None

**Request body:** The response object from `POST https://api.encedo.com/checkin`:
```json
{
  "checked": "<opaque_backend_response>"
}
```

**Response 200:**
```json
{
  "status": "OK"
}
```

**Side effects:** Sets the RTC clock, verifies firmware integrity, and may signal TLS/firmware update availability.

---

#### GET /api/system/config

Reads device configuration.

- **Auth required**: Yes
- **Allowed roles**: User, Master, External
- **Required scope**: `system:config`

**Response 200:**
```json
{
  "eid": "<device_entity_id>",
  "user": "John Doe",
  "email": "user@example.com",
  "hostname": "my.ence.do",
  "uts": 1659436962
}
```

| Field | Type | Description |
|---|---|---|
| `eid` | string | Device entity ID (used as PBKDF2 salt) |
| `user` | string | Configured user name (max 64 chars) |
| `email` | string | Configured email |
| `hostname` | string | Device FQDN |
| `uts` | integer | Last configuration update timestamp |

---

#### POST /api/system/config

Updates device configuration.

- **Auth required**: Yes
- **Allowed roles**: User, Master, External
- **Required scope**: `system:config`

**Request body** (all fields optional, at least one required):
```json
{
  "user": "New Name",
  "tls": { "...cert data from backend..." },
  "wipeout": true
}
```

| Field | Type | Description |
|---|---|---|
| `user` | string | New user name (max 64 chars) |
| `tls` | object | TLS certificate data (from domain registration backend) |
| `wipeout` | boolean | If `true`, factory-resets the device |

**Response 200:**
```json
{
  "updated": true,
  "reboot_required": true
}
```

**Validation:**
- `user` field max 64 characters (returns 400 if exceeded)
- Unknown fields are silently ignored (`updated` = false if no recognized fields)

---

#### GET /api/system/config/attestation

Returns device attestation data (PPA only).

- **Auth required**: Yes (any valid token, scope not checked)
- **Allowed roles**: User, Master, External

**Response 200:**
```json
{
  "genuine": "<attestation_blob>"
}
```

Returns **404** on EPA. Returns **409** if device is in failure state.

---

#### POST /api/system/config/provisioning

Provisions the device (PPA only, one-time).

- **Auth required**: Yes
- **Required scope**: `system:config`

**Request body:**
```json
{
  "crt": "<certificate_data>",
  "genuine": "<attestation_data>"
}
```

Returns **403** if device is already provisioned. Returns **404** on EPA.

---

#### GET /api/system/reboot

Reboots the device.

- **Auth required**: Yes (when device is initialized)
- **Required scope**: Any valid scope
- **Allowed roles**: User, Master, External

**Response 200:**
```json
{
  "status": "OK"
}
```

---

#### GET /api/system/shutdown

Shuts down the device (PPA only).

- **Auth required**: Yes
- **Required scope**: Any valid scope

**Response 200:**
```json
{
  "status": "OK"
}
```

---

#### GET /api/system/selftest

Triggers a full self-test (KAT -- Known Answer Tests).

- **Auth required**: Yes
- **Required scope**: Any valid scope (e.g., `system:config`)

**Response 200 (in progress):**
```json
{
  "kat_busy": true
}
```

**Response 200 (complete):**
```json
{
  "fls_state": 0
}
```

Poll this endpoint until `kat_busy` is absent. KAT may take up to 240 seconds.

---

### 2.2 Authentication

#### GET /api/auth/init

Gets the initialization challenge (personalization phase 1). Only available on un-initialized devices.

- **Auth required**: No
- **Scopes**: None

**Response 200:**
```json
{
  "eid": "<entity_id>",
  "spk": "<device_x25519_public_key_base64>",
  "jti": "<challenge_nonce>",
  "exp": 1659437000
}
```

**Response 406:** Device already initialized.

---

#### POST /api/auth/init

Completes device personalization (phase 2).

- **Auth required**: No
- **Scopes**: None

**Request body:**
```json
{
  "init": "<ejwt_signed_init_payload>"
}
```

The eJWT payload contains:
```json
{
  "jti": "<from_challenge>",
  "aud": "<device_spk>",
  "exp": "<from_challenge>",
  "iat": "<current_timestamp>",
  "iss": "<admin_public_key_base64>",
  "cfg": {
    "masterkey": "<admin_public_key_base64>",
    "userkey": "<user_public_key_base64>",
    "user": "User Name",
    "email": "user@example.com",
    "hostname": "prefix.ence.do",
    "ip": "192.168.7.1",
    "storage_mode": 81,
    "storage_disk0size": 8388608,
    "dnsd": false,
    "trusted_ts": true,
    "trusted_backend": true,
    "allow_keysearch": true,
    "gen_csr": true,
    "origin": "*"
  }
}
```

| Config field | Type | Description |
|---|---|---|
| `masterkey` | string | Admin X25519 public key (base64) |
| `userkey` | string | User X25519 public key (base64) |
| `user` | string | User display name |
| `email` | string | User email |
| `hostname` | string | Device FQDN |
| `ip` | string | Device IP address (PPA only, EPA ignores) |
| `storage_mode` | integer | Storage configuration (PPA only) |
| `storage_disk0size` | integer | Disk0 size in bytes (PPA only) |
| `dnsd` | boolean | Enable DNS daemon (PPA only) |
| `trusted_ts` | boolean | Trust backend timestamps |
| `trusted_backend` | boolean | Trust backend for check-in |
| `allow_keysearch` | boolean | Allow unauthenticated key search by `descr` |
| `gen_csr` | boolean | Generate CSR during init |
| `origin` | string | CORS origin |

**Response 200:**
```json
{
  "instanceid": "<instance_id>",
  "token": "<initial_jwt_token>",
  "csr": "<certificate_signing_request>",
  "genuine": "<attestation_data>"
}
```

---

#### GET /api/auth/token

Gets authentication challenge for local user/admin login.

- **Auth required**: No
- **Scopes**: None

**Response 200:**
```json
{
  "eid": "<entity_id>",
  "spk": "<device_x25519_public_key_base64>",
  "jti": "<challenge_nonce>",
  "exp": 1659437000,
  "lbl": "<label>"
}
```

---

#### POST /api/auth/token

Exchanges an eJWT authentication payload for a JWT access token.

- **Auth required**: No
- **Scopes**: None

**Request body:**
```json
{
  "auth": "<ejwt_signed_auth_payload>"
}
```

The eJWT payload contains:
```json
{
  "jti": "<from_challenge>",
  "aud": "<device_spk>",
  "exp": "<requested_token_expiry>",
  "iat": "<current_timestamp>",
  "iss": "<user_public_key_base64>",
  "scope": "system:config",
  "ctx": "<optional_context_max_64_chars>"
}
```

**Response 200:**
```json
{
  "token": "<jwt_access_token>"
}
```

The returned JWT contains: `sub` (role: `U`/`M`), `scope`, `exp`, `iat`, `jti`.

**Security behaviors (FPT_AFL.1):**
- Minimum response time: 500ms
- After 3 failed attempts: response time increases to 3x (1500ms)
- Throttle resets after ~15 seconds of successful authentications

**Response 401:** Invalid credentials.

---

#### POST /api/auth/ext/init

Initiates external authenticator pairing (registration phase 1).

- **Auth required**: Yes
- **Allowed roles**: User only (Master returns 403)
- **Required scope**: `system:config`

**Request body:**
```json
{
  "epk": "<backend_session_public_key_base64>"
}
```

**Response 200:**
```json
{
  "eid": "<entity_id>",
  "request": "<pairing_challenge>"
}
```

---

#### POST /api/auth/ext/validate

Completes external authenticator pairing (registration phase 2).

- **Auth required**: Yes
- **Allowed roles**: User
- **Required scope**: `system:config`

**Request body:**
```json
{
  "pid": "<pairing_id>",
  "reply": "<authenticator_reply>"
}
```

**Response 200:**
```json
{
  "confirmation": "<confirmation_data>"
}
```

---

#### POST /api/auth/ext/request

Initiates remote authentication (phase 1). No JWT required -- this is the entry point for unauthenticated remote login.

- **Auth required**: No
- **Scopes**: None

**Request body:**
```json
{
  "epk": "<backend_session_public_key>",
  "exp": 1659437120,
  "scope": "system:config"
}
```

**Response 200:**
```json
{
  "eid": "<entity_id>",
  "challenge": "<auth_challenge_data>"
}
```

The entire response must be forwarded to the notification broker (`POST api.encedo.com/notify/event/new`).

---

#### POST /api/auth/ext/token

Exchanges an external auth reply for a JWT token (phase 2).

- **Auth required**: No
- **Scopes**: None

**Request body:**
```json
{
  "authreply": "<signed_auth_reply_from_mobile_app>"
}
```

**Response 200:**
```json
{
  "token": "<jwt_access_token>"
}
```

---

### 2.3 Key Management

#### POST /api/keymgmt/create

Generates a new key in the HSM repository.

- **Auth required**: Yes
- **Required scope**: `keymgmt:gen`

**Request body:**
```json
{
  "label": "my-key",
  "type": "AES256",
  "descr": "<base64_encoded_binary_description>",
  "mode": "ECDH,ExDSA"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | Yes | Key label (max 31 chars) |
| `type` | string | Yes | Key type (see table below) |
| `descr` | string | No | Base64-encoded binary description (max 128 bytes raw) |
| `mode` | string | No | Key mode for NIST ECC keys: `"ECDH"`, `"ExDSA"`, or `"ECDH,ExDSA"` |

**Supported key types:**

| Category | Types |
|---|---|
| NIST ECC | `SECP256R1`, `SECP384R1`, `SECP521R1`, `SECP256K1` |
| Curve25519/448 | `CURVE25519`, `CURVE448`, `ED25519`, `ED448` |
| HMAC | `SHA2-256`, `SHA2-384`, `SHA2-512`, `SHA3-256`, `SHA3-384`, `SHA3-512` |
| AES | `AES128`, `AES192`, `AES256` |
| Post-Quantum KEM | `MLKEM512`, `MLKEM768`, `MLKEM1024` |
| Post-Quantum DSA | `MLDSA44`, `MLDSA65`, `MLDSA87` |

**Response 200:**
```json
{
  "kid": "<32_char_hex_key_id>"
}
```

---

#### POST /api/keymgmt/derive

Derives a new key via ECDH from an existing key and an external public key.

- **Auth required**: Yes
- **Required scope**: `keymgmt:gen`

**Request body:**
```json
{
  "label": "derived-key",
  "type": "AES256",
  "descr": "<base64>",
  "kid": "<ecdh_private_key_kid>",
  "pubkey": "<peer_public_key_base64>",
  "mode": "ECDH,ExDSA"
}
```

**Response 200:**
```json
{
  "kid": "<new_key_id>"
}
```

**Response 406:** ECDH shared secret is too small for the requested key type.

**Note:** ML-KEM and ML-DSA key types cannot be derived -- they must be generated via `/create`.

---

#### POST /api/keymgmt/import

Imports an external public key into the repository.

- **Auth required**: Yes
- **Required scope**: `keymgmt:imp`

**Request body:**
```json
{
  "label": "peer-key",
  "type": "CURVE25519",
  "pubkey": "<public_key_base64>",
  "descr": "<base64>",
  "mode": "ECDH"
}
```

**Response 200:**
```json
{
  "kid": "<key_id>"
}
```

---

#### GET /api/keymgmt/list/{offset}/{limit}

Lists keys in the repository with pagination.

- **Auth required**: Yes
- **Required scope**: `keymgmt:list`
- **Path parameters**: `offset` (default 0), `limit` (optional)

**Response 200:**
```json
{
  "total": 42,
  "listed": 10,
  "list": [
    {
      "kid": "<key_id>",
      "label": "my-key",
      "type": "PKEY,ECDH,ExDSA,SECP256R1",
      "created": 1659436962,
      "updated": 1659436962,
      "descr": "<base64>"
    }
  ]
}
```

The `type` field is a comma-separated string of key attributes:
- `PKEY` -- has private key material
- `ECDH` -- supports ECDH operations
- `ExDSA` -- supports signature operations
- Last element is the key algorithm (e.g., `SECP256R1`, `AES256`)

---

#### GET /api/keymgmt/get/{kid}

Retrieves the public key and metadata for a specific key.

- **Auth required**: Yes
- **Required scope**: `keymgmt:list` or `keymgmt:use:<kid>`

**Response 200:**
```json
{
  "pubkey": "<public_key_base64>",
  "type": "PKEY,ECDH,CURVE25519",
  "updated": 1659436962
}
```

---

#### POST /api/keymgmt/update

Updates a key's label and/or description.

- **Auth required**: Yes
- **Required scope**: `keymgmt:upd`

**Request body:**
```json
{
  "kid": "<key_id>",
  "label": "new-label",
  "descr": "<base64>"
}
```

**Response 200:** Empty or acknowledgement object.

---

#### POST /api/keymgmt/search

Searches keys by the `descr` field using pattern matching.

- **Auth required**: Optional (unauthenticated if `allow_keysearch` is enabled and pattern >= 6 bytes)
- **Required scope**: `keymgmt:search` (when authenticated)

**Request body:**
```json
{
  "descr": "^Q0NURVNU",
  "offset": 0,
  "limit": 10
}
```

The `descr` value is base64-encoded. Prefix with `^` for starts-with matching.

**Response 200:** Same format as `/api/keymgmt/list`.
**Response 404:** No keys match the pattern.

---

#### DELETE /api/keymgmt/delete/{kid}

Deletes a key from the repository.

- **Auth required**: Yes
- **Required scope**: `keymgmt:del`

**Response 200:** Key deleted.
**Response 409:** Cannot delete -- device in failure state.

---

### 2.4 Cryptographic Operations

#### POST /api/crypto/hmac/hash

Computes an HMAC over a message.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<hmac_key_id>",
  "msg": "<base64_encoded_message>",
  "alg": "SHA2-256",
  "ext_kid": "<optional_ecdh_peer_kid>",
  "pubkey": "<optional_ecdh_peer_pubkey_base64>"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `kid` | string | Yes | Key ID (HMAC key or ECDH private key) |
| `msg` | string | Yes | Base64-encoded message (max 2048 bytes raw) |
| `alg` | string | No | Hash algorithm (required when using ECDH-derived HMAC) |
| `ext_kid` | string | No | KID of imported peer public key for indirect ECDH |
| `pubkey` | string | No | Peer public key (base64) for direct ECDH |

When `ext_kid` or `pubkey` is provided, the HMAC key is derived via ECDH: `HMAC_key = Hash(X25519(kid_priv, peer_pub))`.

**Response 200:**
```json
{
  "mac": "<base64_encoded_hmac>"
}
```

---

#### POST /api/crypto/hmac/verify

Verifies an HMAC.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<hmac_key_id>",
  "msg": "<base64_message>",
  "mac": "<base64_mac_to_verify>",
  "alg": "SHA2-256",
  "ext_kid": "<optional>",
  "pubkey": "<optional>"
}
```

**Response 200:** HMAC is valid.
**Response 401/403:** HMAC verification failed or access denied.

---

#### POST /api/crypto/exdsa/sign

Creates a digital signature (ECDSA or EdDSA).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<signing_key_id>",
  "msg": "<base64_message>",
  "alg": "Ed25519",
  "ctx": "<optional_base64_context>"
}
```

**Supported algorithms:**

| Key Type | Algorithms |
|---|---|
| ED25519 | `Ed25519`, `Ed25519ph`, `Ed25519ctx` |
| ED448 | `Ed448`, `Ed448ph` |
| SECP256R1/K1 | `SHA256WithECDSA`, `SHA384WithECDSA`, `SHA512WithECDSA` |
| SECP384R1 | `SHA256WithECDSA`, `SHA384WithECDSA`, `SHA512WithECDSA` |
| SECP521R1 | `SHA256WithECDSA`, `SHA384WithECDSA`, `SHA512WithECDSA` |

The `ctx` field is required for `Ed25519ph`, `Ed25519ctx`, `Ed448`, and `Ed448ph`.

**Response 200:**
```json
{
  "sign": "<base64_encoded_signature>"
}
```

---

#### POST /api/crypto/exdsa/verify

Verifies a digital signature.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<key_id>",
  "msg": "<base64_message>",
  "alg": "Ed25519",
  "sign": "<base64_signature>",
  "ctx": "<optional_base64_context>"
}
```

**Response 200:** Signature valid.

---

#### POST /api/crypto/ecdh

Performs an ECDH key agreement.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body (external pubkey):**
```json
{
  "kid": "<private_key_id>",
  "pubkey": "<peer_public_key_base64>",
  "alg": "SHA2-256"
}
```

**Request body (internal key):**
```json
{
  "kid": "<private_key_id>",
  "ext_kid": "<imported_peer_key_id>",
  "alg": "SHA2-256"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `kid` | string | Yes | Local private key ID |
| `pubkey` | string | One of | Peer public key (base64). Mutually exclusive with `ext_kid` |
| `ext_kid` | string | One of | KID of imported peer public key. Mutually exclusive with `pubkey` |
| `alg` | string | No | Hash algorithm for output: `SHA2-256`, `SHA2-384`, `SHA2-512`, `SHA3-256`, `SHA3-384`, `SHA3-512`. Omit for raw ECDH output |

**Supported key types:** `CURVE25519`, `CURVE448`, `SECP256R1`, `SECP384R1`, `SECP521R1`, `SECP256K1`

**Response 200:**
```json
{
  "ecdh": "<base64_shared_secret>"
}
```

---

#### POST /api/crypto/cipher/encrypt

Encrypts data using AES.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<aes_key_id>",
  "alg": "AES256-GCM",
  "msg": "<base64_plaintext>",
  "aad": "<optional_base64_aad>",
  "ext_kid": "<optional_ecdh_peer_kid>",
  "pubkey": "<optional_ecdh_peer_pubkey>",
  "ctx": "<optional_base64_context>"
}
```

**Supported algorithms:** `AES128-ECB`, `AES192-ECB`, `AES256-ECB`, `AES128-CBC`, `AES192-CBC`, `AES256-CBC`, `AES128-GCM`, `AES192-GCM`, `AES256-GCM`

**Constraints:**
- ECB mode: message length must be a multiple of 16 bytes
- CBC mode: any message size (PKCS#7 padding applied)
- GCM mode: any message size

When `ext_kid` or `pubkey` is provided, the AES key is derived via ECDH + HKDF:
`raw = X25519(kid_priv, peer_pub)` -> `aesKey = HKDF-SHA256(raw, salt=nil, info="encedo-aes", L=32)`

**Response 200:**
```json
{
  "ciphertext": "<base64>",
  "iv": "<base64_iv>",
  "tag": "<base64_gcm_tag>"
}
```

| Field | Present | Description |
|---|---|---|
| `ciphertext` | Always | Base64-encoded ciphertext |
| `iv` | CBC, GCM | Initialization vector (16 bytes for GCM, 16 bytes for CBC) |
| `tag` | GCM only | Authentication tag (16 bytes) |

---

#### POST /api/crypto/cipher/decrypt

Decrypts data using AES.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<aes_key_id>",
  "alg": "AES256-GCM",
  "msg": "<base64_ciphertext>",
  "iv": "<base64_iv>",
  "tag": "<base64_gcm_tag>",
  "aad": "<optional_base64_aad>",
  "ext_kid": "<optional>",
  "pubkey": "<optional>",
  "ctx": "<optional>"
}
```

**Response 200:**
```json
{
  "plaintext": "<base64_decrypted_data>"
}
```

---

#### POST /api/crypto/cipher/wrap

Wraps (encrypts) key material using NIST AES Key Wrap (RFC 3394).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<wrapping_key_id>",
  "alg": "AES256",
  "msg": "<base64_key_material_to_wrap>",
  "ext_kid": "<optional>",
  "pubkey": "<optional>",
  "ctx": "<optional>"
}
```

| Field | Description |
|---|---|
| `alg` | `AES128`, `AES192`, or `AES256` |
| `msg` | Key material to wrap (must be multiple of 8 bytes, minimum 16 bytes) |

**Response 200:**
```json
{
  "wrapped": "<base64_wrapped_key>"
}
```

---

#### POST /api/crypto/cipher/unwrap

Unwraps (decrypts) key material.

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<wrapping_key_id>",
  "alg": "AES256",
  "msg": "<base64_wrapped_key>",
  "ext_kid": "<optional>",
  "pubkey": "<optional>",
  "ctx": "<optional>"
}
```

**Response 200:**
```json
{
  "unwrapped": "<base64_key_material>"
}
```

---

### 2.5 Post-Quantum Cryptography

#### POST /api/crypto/pqc/mlkem/encaps

Performs ML-KEM encapsulation (FIPS 203).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<mlkem_key_id>"
}
```

**Supported key types:** `MLKEM512`, `MLKEM768`, `MLKEM1024`

**Response 200:**
```json
{
  "ss": "<base64_shared_secret>",
  "ct": "<base64_ciphertext>"
}
```

---

#### POST /api/crypto/pqc/mlkem/decaps

Performs ML-KEM decapsulation (FIPS 203).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<mlkem_key_id>",
  "ct": "<base64_ciphertext_from_encaps>"
}
```

**Response 200:**
```json
{
  "ss": "<base64_shared_secret>"
}
```

The shared secret from decapsulation must match the one from encapsulation.

---

#### POST /api/crypto/pqc/mldsa/sign

Creates a post-quantum digital signature (FIPS 204).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<mldsa_key_id>",
  "msg": "<base64_message>"
}
```

**Supported key types:** `MLDSA44`, `MLDSA65`, `MLDSA87`

**Response 200:**
```json
{
  "sign": "<base64_signature>"
}
```

---

#### POST /api/crypto/pqc/mldsa/verify

Verifies a post-quantum digital signature (FIPS 204).

- **Auth required**: Yes
- **Required scope**: `keymgmt:use:<kid>`
- **TLS required**: Yes

**Request body:**
```json
{
  "kid": "<mldsa_key_id>",
  "msg": "<base64_message>",
  "sign": "<base64_signature>"
}
```

**Response 200:** Signature valid.

---

### 2.6 Audit Log

#### GET /api/logger/key

Returns the ED25519 public key used to sign audit log entries.

- **Auth required**: Yes
- **Required scope**: `logger:get`

**Response 200:**
```json
{
  "key": "<ed25519_public_key_base64>",
  "nonce": "<random_nonce_base64>",
  "nonce_signed": "<ed25519_signature_of_nonce_base64>"
}
```

Verify authenticity: `Ed25519_verify(key, nonce, nonce_signed)` must return true.

---

#### GET /api/logger/list/{offset}

Lists available log files (PPA only).

- **Auth required**: Yes
- **Required scope**: `logger:get`

**Response 200:**
```json
{
  "id": [1, 2, 3, 4]
}
```

Returns **404** on EPA.

---

#### GET /api/logger/{id}

Downloads a specific log file (PPA only).

- **Auth required**: Yes
- **Required scope**: `logger:get`

**Response 200:** Plain text log file. Each entry is pipe-delimited with 7 fields.

Returns **404** on EPA.

**Note:** On EPA, logs are streamed via MQTT to a central syslog server and retrieved via the Encedo backend API.

---

### 2.7 Storage (PPA only)

#### GET /api/storage/unlock

Unlocks a storage disk.

- **Auth required**: Yes
- **Required scope**: `storage:disk0:rw` or `storage:disk0:ro` for disk0; `storage:disk1:rw` or `storage:disk1:ro` for disk1

The disk affected depends on the scope in the JWT token.

**Response 200:** Disk unlocked.
**Response 404:** Not available on EPA.

---

#### GET /api/storage/lock

Locks a storage disk.

- **Auth required**: Yes
- **Required scope**: `storage:disk0:rw` or `storage:disk1:rw` (matching the disk to lock)

**Response 200:** Disk locked.
**Response 404:** Not available on EPA.

---

### 2.8 Firmware Upgrade

#### GET /api/system/upgrade/usbmode

Activates USB DFU mode for firmware upgrade via serial (PPA only).

- **Auth required**: Yes
- **Required scope**: `system:upgrade`

**Response 200:** Device enters USB mode.

---

#### POST /api/system/upgrade/upload_fw

Uploads a firmware binary for OTA upgrade.

- **Auth required**: Yes
- **Required scope**: `system:upgrade`
- **Content-Type**: Binary upload (filename: `firmware.bin`)

**Response 200:** Upload accepted.

---

#### GET /api/system/upgrade/check_fw

Checks the integrity of an uploaded firmware image.

- **Auth required**: Yes
- **Required scope**: `system:upgrade`

**Response 200:** Firmware verified.
**Response 202:** Verification in progress (poll until 200).

---

#### GET /api/system/upgrade/install_fw

Installs verified firmware and reboots.

- **Auth required**: Yes
- **Required scope**: `system:upgrade`

**Response 200:** Installation started. Device will reboot.

---

#### POST /api/system/upgrade/upload_ui

Uploads a UI/dashboard archive (PPA only).

- **Auth required**: Yes
- **Required scope**: `system:upgrade`
- **Content-Type**: Binary upload (filename: `webroot.tar`)

**Response 200:** Upload accepted.

---

#### GET /api/system/upgrade/check_ui

Checks the integrity of an uploaded UI archive.

- **Auth required**: Yes
- **Required scope**: `system:upgrade`

**Response 200:** UI verified.
**Response 202:** Verification in progress.

---

#### GET /api/system/upgrade/install_ui

Installs verified UI and restarts web server.

- **Auth required**: Yes
- **Required scope**: `system:upgrade`

**Response 200:** Installation complete.

---

### 2.9 Diagnostics (DIAG firmware only)

These endpoints are only available when the firmware is built with the DIAG module (`fwv` contains "-DIAG"). They are used for Common Criteria evaluation testing.

| Endpoint | Method | Description |
|---|---|---|
| `/api/diag/break_temp` | GET | Simulates temperature out-of-range (triggers FLS) |
| `/api/diag/break_tls` | GET | Injects TLS packet corruption |
| `/api/diag/break_trng` | GET | Corrupts TRNG entropy source (triggers FLS after ~15s) |
| `/api/diag/test_trng` | GET | Returns raw random bytes: `{"rnd": "<base64>"}` |
| `/api/diag/wipe_config` | GET | Wipes device configuration |
| `/api/diag/memdump/{addr}/{len}` | GET | Returns memory dump: `{"dump": "<base64>"}` |
| `/api/diag/corrupt_repo/{addr}` | GET | Corrupts key repository at address |

---

## 3. Access Scope Reference

| Scope | Grants access to |
|---|---|
| `system:config` | `/api/system/config` (GET/POST), `/api/system/reboot`, `/api/system/shutdown`, `/api/system/selftest` |
| `system:upgrade` | `/api/system/upgrade/*` |
| `system:reboot` | `/api/system/reboot` (narrower than `system:config`) |
| `system:shutdown` | `/api/system/shutdown` |
| `keymgmt:gen` | `/api/keymgmt/create`, `/api/keymgmt/derive` |
| `keymgmt:imp` | `/api/keymgmt/import` |
| `keymgmt:list` | `/api/keymgmt/list`, `/api/keymgmt/get` |
| `keymgmt:upd` | `/api/keymgmt/update` |
| `keymgmt:del` | `/api/keymgmt/delete` |
| `keymgmt:search` | `/api/keymgmt/search` |
| `keymgmt:use:<kid>` | All crypto operations on the specific key identified by `<kid>` |
| `logger:get` | `/api/logger/key`, `/api/logger/list`, `/api/logger/{id}` |
| `storage:disk0:rw` | `/api/storage/unlock` (read-write), `/api/storage/lock` for disk0 |
| `storage:disk0:ro` | `/api/storage/unlock` (read-only) for disk0 |
| `storage:disk1:rw` | `/api/storage/unlock` (read-write), `/api/storage/lock` for disk1 |
| `storage:disk1:ro` | `/api/storage/unlock` (read-only) for disk1 |

---

## 4. External Backend API (api.encedo.com)

The HEM device relies on the Encedo backend for several flows. These are **not** HEM device endpoints but are documented here because they are part of the integration workflow.

| Endpoint | Method | Description |
|---|---|---|
| `/checkin` | POST | Processes check-in challenge, returns signed response |
| `/domain/check/{prefix}` | GET | Checks domain prefix availability (404 = available) |
| `/domain/register/{prefix}/{authkey}` | POST | Registers domain and issues TLS certificate |
| `/domain/register/{id}` | GET | Polls for certificate issuance status |
| `/notify/session` | GET/POST | Creates a notification broker session, returns `epk` |
| `/notify/register/init` | POST | Initiates external authenticator registration |
| `/notify/register/check/{rid}` | GET | Polls registration status (202 = pending, 200 = done) |
| `/notify/register/finalise/{rid}` | POST | Finalizes authenticator registration |
| `/notify/event/new` | POST | Sends authentication notification to mobile app |
| `/notify/event/check/{eventid}` | GET | Polls auth event status (202 = pending, 200 = responded) |

---

## 5. PPA vs EPA Differences

| Feature | PPA | EPA |
|---|---|---|
| Form factor | USB device | 19" rack appliance |
| Default domain | `my.ence.do` | `*.cloud.ence.do` |
| Default IP | `192.168.7.1` | Network-configured |
| Storage endpoints | Available (`/api/storage/*`) | Returns 404 |
| Log file download | Available (`/api/logger/list`, `/api/logger/{id}`) | Returns 404 (uses MQTT syslog) |
| UI upgrade | Available (`upload_ui`, `check_ui`, `install_ui`) | Not applicable |
| USB DFU upgrade | Available (`/api/system/upgrade/usbmode`) | Not applicable |
| Shutdown | Available (`/api/system/shutdown`) | Use reboot |
| Init config fields | All fields used | `ip`, `storage_mode`, `storage_disk0size`, `dnsd` ignored |

---

## 6. Typical Integration Flows

### 6.1 Device Onboarding (first-time setup)

```
1. GET  /api/system/status          -- verify device is reachable, check 'inited' field
2. GET  /api/system/version         -- identify PPA vs EPA
3. GET  /api/system/checkin         -- get challenge
4. POST api.encedo.com/checkin      -- forward to backend
5. POST /api/system/checkin         -- complete check-in (sets RTC)
6. GET  /api/auth/init              -- get init challenge
7. POST /api/auth/init              -- personalize device (cfg + keys)
8. POST api.encedo.com/domain/...   -- register domain + get TLS cert
9. POST /api/system/config          -- upload TLS cert ({"tls": ...})
10. GET /api/system/reboot          -- reboot to apply
```

### 6.2 Local User Authentication

```
1. GET  /api/auth/token             -- get challenge (eid, spk, jti, exp)
2. Derive: seed = PBKDF2(password, eid, 600000)
3. Derive: pubkey = X25519(seed, basepoint)
4. Build eJWT with scope, sign with X25519(seed, spk)
5. POST /api/auth/token             -- {"auth": "<ejwt>"} -> {"token": "<jwt>"}
```

### 6.3 Remote (External) Authentication

```
1. GET  api.encedo.com/notify/session           -- get broker session (epk)
2. POST /api/auth/ext/request                   -- get challenge from device
3. POST api.encedo.com/notify/event/new         -- send push notification
4. GET  api.encedo.com/notify/event/check/{id}  -- poll until user responds
5. POST /api/auth/ext/token                     -- exchange authreply for JWT
```

### 6.4 Key Generation + Crypto Operation

```
1. Authenticate with scope "keymgmt:gen"
2. POST /api/keymgmt/create         -- {"label":"my-key", "type":"AES256"}
3. Note the returned KID
4. Authenticate with scope "keymgmt:use:<KID>"
5. POST /api/crypto/cipher/encrypt  -- {"kid":"<KID>", "alg":"AES256-GCM", "msg":"<b64>"}
```
