# Open Questions — API Documentation Gaps

Issues found in `HEM-REST-API-DESIGN.md` and `INTEGRATION_GUIDE.md` that block
implementation or were discovered during testing. Each item notes which endpoint is
affected and what specifically is missing or wrong.

---

## Critical (blocks implementation)

### OQ-1 — Notification broker URL is wrong

**Affected endpoints:** `POST /api/auth/ext/init` (pairing), `POST /api/auth/ext/request` (login)

The API design states the device response must be forwarded to:
```
POST api.encedo.com/notify/event/new
```
Testing confirmed this URL returns **HTTP 404**. The check-in broker
(`https://api.encedo.com/checkin`) works, so the server is reachable — only
this path is wrong.

**Blocked:** `hem_auth_ext_pair` and `hem_auth_ext_login` are stubbed out until
the correct URL is confirmed.

**Questions:**
- What is the correct broker URL?
- Does the URL include the device `eid` as a path segment (e.g. `.../event/<eid>`)?
- Is there a query parameter or request header required?

---

### OQ-2 — Notification broker request/response format undefined

**Affected endpoints:** same as OQ-1

The API design says "forward the entire response verbatim" but does not document
what the broker returns.

For **pairing** (`ext/init` → broker → `ext/validate`):
- The broker response is assumed to contain `pid` and `reply` fields (based on
  what `ext/validate` expects), but this is not stated anywhere.

For **login** (`ext/request` → broker → `ext/token`):
- The broker response is assumed to contain an `authreply` field (based on what
  `ext/token` expects), but this is not stated anywhere.

**Questions:**
- What JSON fields does the broker return for pairing? (`pid`, `reply`?)
- What JSON fields does the broker return for login? (`authreply`?)
- Does the broker block synchronously until the phone approves, or does it return
  immediately and require polling? If polling, what is the polling endpoint?
- Is there a timeout on the broker side? What HTTP status is returned on timeout?

---

### OQ-3 — `epk` field: who generates it and in what format?

**Affected endpoints:** `POST /api/auth/ext/init`, `POST /api/auth/ext/request`

Both endpoints accept an `epk` described as "backend session public key (base64)".
The word "backend" is ambiguous — it could mean:
- The calling application generates an ephemeral X25519 keypair and passes the
  public key (this is the current assumption in the implementation).
- The Encedo cloud backend generates a session keypair, and `epk` must be
  retrieved from there first.

If the application generates it, the corresponding private key must be used to
decrypt something in the broker response (which would explain why the broker
response format is undefined — it may be encrypted).

**Questions:**
- Does the client (library) generate the `epk` keypair, or does it come from
  the Encedo cloud backend?
- Is the broker response encrypted to `epk`? If so, what encryption scheme?
- Should the library generate and manage the ephemeral keypair internally, or
  should the caller provide both the public and private key?

---

## Significant (causes unclear behaviour)

### OQ-4 — `lbl` field in `GET /api/auth/token` response is undocumented

**Affected endpoint:** `GET /api/auth/token`

Response includes a `lbl` field:
```json
{ "eid": "...", "spk": "...", "jti": "...", "exp": ..., "lbl": "<label>" }
```
The `lbl` field is present in the spec but never explained. The library currently
ignores it.

**Questions:**
- What does `lbl` contain? (Device label? User label?)
- Should the library expose it to callers?
- Is it required to be included in the eJWT payload?

---

### OQ-5 — `ctx` field in `POST /api/auth/token` eJWT payload is undocumented

**Affected endpoint:** `POST /api/auth/token`

The eJWT payload spec lists an optional `ctx` field: "optional context max 64 chars".
The purpose, format, and effect of this field are not explained. The library
currently never sends it.

**Questions:**
- What is `ctx` used for on the device side?
- Is it a human-readable string, a base64 blob, or something else?
- Does it affect the issued token in any way (e.g. embedded in the JWT claims)?

---

### OQ-6 — `POST /api/keymgmt/update` response format undefined

**Affected endpoint:** `POST /api/keymgmt/update`

Response is described as "Empty or acknowledgement object" with no JSON example.
The library cannot tell whether the update succeeded other than by checking HTTP 200.

**Questions:**
- Is the response body always empty `{}`?
- Is there an `updated: true/false` field similar to `POST /api/system/config`?

---

### OQ-7 — `POST /api/keymgmt/search` minimum pattern length for unauthenticated use

**Affected endpoint:** `POST /api/keymgmt/search`

The spec says unauthenticated search is allowed "if `allow_keysearch` is enabled
and pattern >= 6 bytes". It is unclear whether "6 bytes" refers to the raw binary
length of the `descr` pattern before base64 encoding, or the length of the
base64 string itself.

**Questions:**
- Is the 6-byte minimum applied before or after base64 encoding?
- What HTTP status is returned if the pattern is too short?

---

### OQ-8 — Audit log entry format only partially described

**Affected endpoint:** `GET /api/logger/{id}`

The spec says the response is "plain text" with entries that are "pipe-delimited
with 7 fields" but does not name the fields.

**Questions:**
- What are the 7 pipe-delimited fields? (timestamp, event type, KID, user, ...?)
- Is the response Content-Type `text/plain` or `application/octet-stream`?
- Is there a maximum file size, or can log files be arbitrarily large?

---

### OQ-9 — Firmware upgrade uses binary upload, not JSON

**Affected endpoints:** `POST /api/system/upgrade/upload_fw`, `POST /api/system/upgrade/upload_ui`

The spec says Content-Type is "Binary upload (filename: firmware.bin / webroot.tar)"
but does not specify:
- Whether this is `multipart/form-data` or raw `application/octet-stream`
- The form field name if multipart
- The maximum accepted file size

The current `hem_http_post` implementation only handles JSON bodies. A new
transport function (`hem_http_post_binary`) will be needed.

**Questions:**
- Is the upload `multipart/form-data` or raw binary with `Content-Type: application/octet-stream`?
- If multipart, what is the form field name?
- What is the maximum firmware image size?

---

### OQ-10 — `GET /api/system/upgrade/check_fw` polling interval not specified

**Affected endpoint:** `GET /api/system/upgrade/check_fw`

Returns HTTP 202 while verification is in progress, 200 when complete. No
recommended polling interval or maximum wait time is given.

**Questions:**
- What is the expected verification duration?
- Is there a timeout after which the device gives up and returns an error?
- Same question applies to `GET /api/system/upgrade/check_ui`.

---

### OQ-11 — Storage scope format for disk indices > 0 not confirmed

**Affected endpoints:** `GET /api/storage/unlock`, `GET /api/storage/lock`

The spec mentions `storage:disk0:rw` and `storage:disk1:rw` in the scope table,
but the format is only illustrated for disk0. The `status` response shows a
`storage` array with entries like `"disk0:rw"`, `"disk1:-"`.

**Questions:**
- Is the scope always `storage:diskN:rw` where N is the zero-based disk index?
- Is `storage:disk0:ro` (read-only unlock) a valid scope? The spec lists it but
  the lock endpoint only mentions `rw` scopes.
- Can both disks be unlocked simultaneously with separate tokens?

---

### OQ-12 — `GET /api/crypto/hmac/verify` and `POST /api/crypto/exdsa/verify` response body not shown

**Affected endpoints:** both verify endpoints

Both are described as "Response 200: HMAC is valid" / "Response 200: Signature valid"
with no example JSON body. It is unclear whether the body is `{}`, absent, or
contains a confirmation field.

**Questions:**
- What is the exact response body on successful verification?
- Is there a body that distinguishes "valid" from "invalid" (vs. relying solely
  on HTTP status codes)?

---

## Minor (low impact, easily worked around)

### OQ-13 — `genuine` attestation blob: format and size unknown

**Affected endpoint:** `GET /api/system/config/attestation`

The `genuine` field is described as opaque. No size guidance is given for
allocating a receive buffer.

**Questions:**
- What is the approximate maximum size of the `genuine` blob?

---

### OQ-14 — Check-in and ext/request response field sizes not specified

**Affected endpoints:** `GET /api/system/checkin`, `POST /api/auth/ext/request`

Both return opaque blobs (`check`, `challenge`) that must be stored and forwarded.
No maximum size is documented. The library currently uses `malloc(resp_len + 1)`
which handles any size, but fixed-size buffers in future language bindings need
this information.

**Questions:**
- What is the maximum size of the `check` challenge from check-in?
- What is the maximum size of the `challenge` from `ext/request`?

---

### OQ-15 — `POST /api/auth/ext/validate` `reply` field size unknown

**Affected endpoint:** `POST /api/auth/ext/validate`

The `reply` value (returned by the notification broker, forwarded to validate)
has no documented size limit. The current stub allocates 1024 bytes for it.

**Questions:**
- What is the maximum size of the `reply` field?

---

## Summary table

| ID | Severity | Topic | Blocks |
|---|---|---|---|
| OQ-1 | Critical | Broker URL returns 404 | `hem_auth_ext_pair`, `hem_auth_ext_login` |
| OQ-2 | Critical | Broker request/response format | Same |
| OQ-3 | Critical | `epk` generation and encryption | Same |
| OQ-4 | Significant | `lbl` field undocumented | Minor — library ignores it |
| OQ-5 | Significant | `ctx` in eJWT payload undocumented | Minor — library omits it |
| OQ-6 | Significant | `update` response format | `hem_key_update` |
| OQ-7 | Significant | Search pattern minimum length | `hem_key_search` |
| OQ-8 | Significant | Log entry field names | `hem_logger_download` |
| OQ-9 | Significant | Firmware binary upload format | `hem_upgrade_upload_fw/ui` |
| OQ-10 | Significant | `check_fw` polling interval | `hem_upgrade_check_fw` |
| OQ-11 | Significant | Storage scope format for disk N | `hem_storage_unlock/lock` |
| OQ-12 | Significant | Verify endpoints response body | `hem_hmac_verify`, `hem_verify` |
| OQ-13 | Minor | `genuine` blob max size | Buffer sizing |
| OQ-14 | Minor | Checkin/challenge blob max sizes | Buffer sizing in bindings |
| OQ-15 | Minor | Broker `reply` field max size | Buffer sizing |
