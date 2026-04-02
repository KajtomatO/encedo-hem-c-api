# Encedo HEM Library -- Integration Guide for Language Bindings

This document captures implementation details, non-obvious behaviors, and lessons
learned from the C reference implementation (`libhem`). Its purpose is to give
agents or developers building bindings in other languages (Python, Rust, Go, etc.)
a complete picture without having to reverse-engineer the device protocol.

---

## 1. What the library does

`libhem` wraps the Encedo HEM (Hardware Encryption Module) REST API. The device
is a USB or rack-mount HSM that **never exposes key material** -- all crypto
operations happen on the device. The library handles:

- Transport (HTTPS to the device)
- The custom eJWT authentication protocol (challenge-response using X25519 + HMAC-SHA256)
- JSON serialization / deserialization
- Token caching and automatic re-authentication
- The two-phase internet check-in flow

The device REST API is documented in `HEM-REST-API-DESIGN.md`.

---

## 2. Transport layer

### 2.1 HTTPS with TLS verification disabled

The device uses HTTPS (TLS 1.3) but its certificate is self-signed or issued by
the Encedo CA, not a public CA. **TLS peer/host verification must be disabled.**
Device authenticity is established out-of-band via the check-in protocol (Section 5).

In libcurl terms: `CURLOPT_SSL_VERIFYPEER = 0`, `CURLOPT_SSL_VERIFYHOST = 0`.
In Python `requests`: `verify=False`. In Rust `reqwest`: `.danger_accept_invalid_certs(true)`.

### 2.2 No connection reuse -- critical

The device's embedded HTTP server **closes the TCP connection after every response**.
If you reuse a connection (keep-alive), the next request will fail with a send error
(libcurl: `CURLE_SEND_ERROR`, Python requests: `ConnectionError`).

**Always open a new connection per request.**

- libcurl: `CURLOPT_FORBID_REUSE = 1`
- Python `requests`: use a plain `requests.get/post()` call (not a `Session`), or
  set `session.headers['Connection'] = 'close'`
- Rust `reqwest`: build the client with `.connection_verbose(true)` or use
  `ClientBuilder` with `pool_max_idle_per_host(0)`

This was discovered empirically -- POST requests after a prior POST would fail
until per-request connections were enforced.

### 2.3 Base URL

The base URL has no trailing slash: `https://my.ence.do`.
All paths begin with `/api/...`.

### 2.4 Content-Type

All request and response bodies are `application/json`.
POST bodies must set `Content-Type: application/json`.

### 2.5 Max POST body

The device rejects bodies larger than **7300 bytes** (5 × MTU) with HTTP 413.
Validate plaintext size before encrypting: base64 overhead means the actual
usable plaintext is ≈ 5475 bytes minus the JSON wrapper.

---

## 3. Authentication protocol (eJWT)

This is the most complex part and the most likely source of bugs in new bindings.

### 3.1 Overview

The device uses a custom challenge-response scheme called **eJWT** (Encedo JWT).
It is a standard JWT (header.payload.signature) but the signing key is derived
from the user's passphrase and the device's X25519 public key via ECDH, rather
than a stored secret.

**No passphrase ever leaves the client machine.**

### 3.2 Step-by-step flow

```
1. GET /api/auth/token
   Response: { "eid": "<salt>", "spk": "<device_x25519_pubkey_b64>",
               "jti": "<nonce>", "exp": <deadline_ts>, "lbl": "<label>" }

2. seed = PBKDF2-SHA256(passphrase, eid, iterations=600000, dklen=32)
   -- 'eid' is the salt, NOT the passphrase
   -- 600000 iterations is intentional (security requirement FPT_AFL.1)
   -- expect this to take ~1 second on a modern CPU

3. privkey = X25519_private_key_from_raw_bytes(seed)
   -- the 32-byte seed IS the private key scalar directly (no further hashing)

4. pubkey  = X25519_public_key_from_private(privkey)
   -- used as the 'iss' claim (standard base64, WITH padding)

5. shared  = X25519_ECDH(privkey, base64_decode(spk))
   -- spk is standard base64, decode to 32 raw bytes first
   -- shared secret is 32 bytes

6. Build JWT:
   header  = {"ecdh":"x25519","alg":"HS256","typ":"JWT"}   (always this exact string)
   payload = {
     "jti":   "<jti from challenge>",
     "aud":   "<spk from challenge -- the original base64 string, NOT decoded>",
     "exp":   <requested_token_expiry_unix_ts>,
     "iat":   <now_unix_ts>,
     "iss":   "<base64_standard(pubkey)>",
     "scope": "<requested_scope>"
   }
   h   = base64url_nopad(utf8(header_json))
   p   = base64url_nopad(utf8(payload_json))
   sig = HMAC-SHA256(key=shared_secret, message=h + "." + p)
   ejwt = h + "." + p + "." + base64url_nopad(sig)

7. POST /api/auth/token
   Body: {"auth": "<ejwt>"}
   Response: {"token": "<jwt_access_token>"}

8. Cache the JWT. Use as: Authorization: Bearer <jwt>
```

### 3.3 Base64 conventions -- two different encodings in use

| Where | Encoding | Padding |
|---|---|---|
| API message payloads (`msg`, `aad`, `ciphertext`, etc.) | Standard base64 (RFC 4648 §4) | Yes (`=`) |
| Device public key `spk` in auth challenge | Standard base64 | Yes |
| User public key `iss` in eJWT payload | Standard base64 | Yes |
| JWT segments (header, payload, signature) | base64**url** (RFC 4648 §5) | **No** (`+`→`-`, `/`→`_`, strip `=`) |

Mixing these up is the most common bug. JWT uses base64url without padding;
everything else uses standard base64 with padding.

### 3.4 Token scopes

Every JWT is bound to **one scope string**. Re-authenticate to change scope.
The library gets a new token automatically when the scope needed differs from
the cached token's scope.

| Operation | Scope |
|---|---|
| Read device config | `system:config` |
| Create/derive keys | `keymgmt:gen` |
| Import public keys | `keymgmt:imp` |
| List / get keys | `keymgmt:list` |
| Update key metadata | `keymgmt:upd` |
| Delete keys | `keymgmt:del` |
| Search keys | `keymgmt:search` |
| Any crypto operation on key X | `keymgmt:use:<KID>` (per-key) |
| Read audit logs | `logger:get` |
| Upgrade firmware | `system:upgrade` |
| Storage lock/unlock | `storage:disk0:rw` etc. |

The `keymgmt:use:<KID>` scope is dynamic -- it embeds the 32-char hex KID.
A new token is needed for each key used in crypto operations, unless the
previous token already covers that KID.

### 3.5 Token expiry

- Request `exp = now + 3600` (1 hour) in the eJWT payload.
- The challenge's `exp` field is the challenge deadline (not the token lifetime).
- Cap `req_exp` at the challenge's `exp` if it is smaller.
- Cached tokens should be considered expired **60 seconds before** their `exp`
  to account for clock skew and network latency.

### 3.6 Role

- `HEM_ROLE_USER` (`sub` = `U`) -- regular operations
- `HEM_ROLE_MASTER` (`sub` = `M`) -- administrative operations

Role is implicit in which X25519 key was used to initialize the device.
The passphrase for user and master are different; use the correct one.

### 3.7 Sensitive material handling

- Zero the 32-byte PBKDF2 seed and the 32-byte ECDH shared secret immediately
  after constructing the eJWT.
- Store the passphrase in a dedicated zeroed buffer; zero it on context destroy.
- The JWT token itself is not sensitive (it is bearer-style, short-lived, scoped).

---

## 4. Key management

### 4.1 KID format

Key IDs are **32 lowercase hex characters** (128-bit identifiers), e.g.:
`0382e3eab596598b1f3582ef90b61a0e`

### 4.2 Key type strings

Pass these exactly as shown to `/api/keymgmt/create`:

| Category | Type strings |
|---|---|
| AES | `AES128`, `AES192`, `AES256` |
| NIST ECC | `SECP256R1`, `SECP384R1`, `SECP521R1`, `SECP256K1` |
| Curve25519/448 | `CURVE25519`, `CURVE448`, `ED25519`, `ED448` |
| HMAC | `SHA2-256`, `SHA2-384`, `SHA2-512`, `SHA3-256`, `SHA3-384`, `SHA3-512` |
| Post-Quantum KEM | `MLKEM512`, `MLKEM768`, `MLKEM1024` |
| Post-Quantum DSA | `MLDSA44`, `MLDSA65`, `MLDSA87` |

### 4.3 Key type string in list response

The `type` field in a list or get response is a **comma-separated attribute string**,
not the creation type. Example: `"PKEY,ECDH,ExDSA,SECP256R1"`. Parse it by
splitting on `,` -- the last element is the algorithm; earlier elements are flags
(`PKEY` = has private key, `ECDH` = supports ECDH, `ExDSA` = supports signing).

### 4.4 Key deletion and FLS state

`DELETE /api/keymgmt/delete/{kid}` returns **HTTP 409** if the device is in a
failure state (`fls_state != 0`). Handle this gracefully. The device's FLS state
is reported in `GET /api/system/status` → `fls_state` bitmask:

| Bit | Value | Meaning |
|---|---|---|
| 0 | 1 | Entropy source failure |
| 1 | 2 | Firmware integrity failure |
| 2 | 4 | Temperature out of range |

A value of 4 (temp only) was observed at 87°C during testing; the device
recovered to FLS=0 after cooling. Crypto operations other than delete were
unaffected at FLS=4.

---

## 5. Check-in protocol

Check-in synchronizes the device with the Encedo backend, sets the RTC clock,
and verifies firmware integrity. It requires **outbound internet access** to
`https://api.encedo.com`.

```
1. GET  {device}/api/system/checkin
   Response body: {"check": "<opaque_challenge>"}

2. POST https://api.encedo.com/checkin
   Body: the entire JSON object from step 1 (forward verbatim)
   Response body: {"checked": "<opaque_response>"}

3. POST {device}/api/system/checkin
   Body: the entire JSON object from step 2 (forward verbatim)
   Response: {"status": "OK"}
```

No authentication required for any step.
Side effects: RTC clock set, firmware integrity verified.
The device's `ts` field in `/api/system/status` will be non-null after check-in.

---

## 6. Crypto operations

### 6.1 Encrypt / Decrypt

All message fields are **standard base64 with padding** in both directions.

Encrypt request:
```json
{"kid": "<kid>", "alg": "AES256-GCM", "msg": "<base64(plaintext)>"}
```

Encrypt response:
```json
{"ciphertext": "<base64>", "iv": "<base64_16bytes>", "tag": "<base64_16bytes>"}
```

Decrypt request:
```json
{"kid": "<kid>", "alg": "AES256-GCM",
 "msg": "<base64(ciphertext)>", "iv": "<base64>", "tag": "<base64>"}
```

Decrypt response:
```json
{"plaintext": "<base64(plaintext)>"}
```

For GCM: IV is 16 bytes, tag is 16 bytes. Both are always present in the response.
For CBC: IV is present, tag is absent. For ECB: neither IV nor tag.

**TLS is required** for all crypto endpoints (`/api/crypto/*`). Since the device
may not have TLS operational (status `https: false`) during early setup, crypto
calls may fail with HTTP 418 until TLS is configured via check-in + provisioning.

### 6.2 Algorithm strings

AES modes: `AES128-ECB`, `AES192-ECB`, `AES256-ECB`,
           `AES128-CBC`, `AES192-CBC`, `AES256-CBC`,
           `AES128-GCM`, `AES192-GCM`, `AES256-GCM`

ECB requires message length to be a multiple of 16 bytes.
CBC and GCM accept any length (CBC uses PKCS#7 padding).

---

## 7. Device status quirks

### 7.1 `inited` field -- inverted logic

In `GET /api/system/status`:
- If the JSON object **contains the key `inited`** → device is **NOT** initialized
- If the key is **absent** → device **IS** initialized

This is opposite to intuition. The C implementation uses:
```c
out->initialized = !cJSON_HasObjectItem(root, "inited");
```

### 7.2 `ts` field absence

The `ts` (RTC timestamp) field is absent from the status response when the RTC
is not set (before check-in). Treat a missing `ts` as -1 / None / null rather
than 0 (0 is a valid Unix timestamp).

### 7.3 `hostname` field in status vs config

`GET /api/system/status` returns `hostname` (always present, may be empty string).
`GET /api/system/config` returns a richer config including `eid`, `user`, `email`.
The `eid` from config is the same as the `eid` in the auth challenge and is needed
for PBKDF2 -- but caching it from the auth challenge is sufficient; no need to
call config just to get eid.

---

## 8. PPA vs EPA differences

| Feature | PPA (USB) | EPA (rack) |
|---|---|---|
| Default hostname | `my.ence.do` | `*.cloud.ence.do` |
| Storage endpoints | Available | HTTP 404 |
| Log file download | Available | HTTP 404 (uses MQTT syslog) |
| Shutdown endpoint | Available | Use reboot instead |
| Hardware version string | Contains "PPA" | Contains "EPA" |

Detect form factor from `GET /api/system/version` → `hwv` field.

---

## 9. Error handling reference

| HTTP Status | Meaning | Library action |
|---|---|---|
| 200 | Success | Parse response body |
| 400 | Bad request (malformed JSON, invalid arg) | Return error + body has detail |
| 401 | Missing or invalid JWT | Re-authenticate and retry once |
| 403 | Wrong scope or role | Check scope string; may need different token |
| 404 | Endpoint not available (EPA vs PPA) or key not found | Surface as not-found error |
| 406 | Device already initialized (auth/init only) | Non-fatal |
| 409 | Device in FLS failure state | Surface as device-failure error |
| 413 | POST body too large (>7300 bytes) | Validate before sending |
| 418 | TLS required for this endpoint | Device needs TLS provisioning |

---

## 10. C library structure (for reference bindings)

```
include/hem/
  hem.h          -- umbrella header (include this only)
  hem_types.h    -- all public structs and enums
  hem_system.h   -- system endpoints (version, status, checkin, config)
  hem_keymgmt.h  -- key management (create, delete, list, get)
  hem_crypto.h   -- crypto operations (encrypt, decrypt)

src/
  hem_ctx.c      -- context lifecycle
  hem_http.c     -- libcurl transport (FORBID_REUSE set here)
  hem_json.c     -- cJSON wrappers + base64 encode/decode
  hem_auth.c     -- eJWT construction + token cache (hem_auth_login, hem_auth_ensure)
  hem_system.c   -- system endpoint implementations
  hem_keymgmt.c  -- key management implementations
  hem_crypto.c   -- crypto endpoint implementations
  internal.h     -- full hem_ctx struct + internal function declarations
```

The `hem_auth_ensure(ctx, scope)` internal function is the key building block:
it checks whether the cached token matches the requested scope and is not near
expiry, and calls `hem_auth_login` if not. Every authenticated endpoint calls
this before making its HTTP request.

---

## 11. Dependency summary

| Dependency | Purpose | Notes |
|---|---|---|
| libcurl | HTTP/HTTPS transport | Any recent version; must support TLS |
| OpenSSL (libcrypto) | PBKDF2-SHA256, X25519, HMAC-SHA256 | v1.1.1+ API used |
| cJSON | JSON parse/build | Vendored in `third_party/cJSON/` (v1.7.19) |

OpenSSL functions used:
- `PKCS5_PBKDF2_HMAC(pass, pass_len, salt, salt_len, 600000, EVP_sha256(), 32, out)`
- `EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, seed_bytes, 32)`
- `EVP_PKEY_get_raw_public_key(pkey, buf, &len)`
- `EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_bytes, 32)`
- `EVP_PKEY_CTX_new` / `EVP_PKEY_derive_init` / `EVP_PKEY_derive_set_peer` / `EVP_PKEY_derive`
- `HMAC(EVP_sha256(), key, key_len, data, data_len, out, &out_len)`
