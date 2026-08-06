# Encedo HEM C SDK — API guide (1.0)

The public headers in `include/ehem/` are the **per-symbol reference**:
every function, struct, enum, and macro carries its full contract there —
parameters, ownership, defaults, error mapping, and the firmware quirks
that shaped it (format decision, REQ-API-007, 2026-08-06). This guide is
the cross-cutting layer the headers cannot give you: the conventions every
call follows, a symbol index per header, and worked examples. A scripted
completeness gate (`check_docs_coverage`, part of the unit suite) keeps
this index in sync with the headers — a new public symbol that is not
indexed here fails the build's test run.

Related documents: [COVERAGE.md](COVERAGE.md) maps every endpoint of the
device API documentation to its SDK binding (or records why it is
deliberately unbound); [KNOWN-ISSUES.md](../KNOWN-ISSUES.md) records the
firmware bugs and doc divergences the SDK works around; `hem-tool`
(`src/tools/hem-tool/`) is the living usage documentation — every binding
is drivable from it.

## Getting started

```c
#include <ehem/ehem.h>
#include <ehem/auth.h>
#include <ehem/keymgmt.h>
#include <stdio.h>

int main(void)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_key_page *keys = NULL;
    ehem_rc rc;
    size_t i;

    ehem_global_init();                    /* once per process (recommended) */

    ehem_options_init(&opts);              /* REQUIRED before overriding fields */
    opts.total_timeout_ms = 60000;         /* zero/absent fields keep defaults */

    rc = ehem_ctx_create("https://my.ence.do", &opts, &ctx);
    if (rc != EHEM_OK) { return 1; }

    rc = ehem_login(ctx, "correct horse battery staple");   /* lazy: no I/O */
    if (rc == EHEM_OK) {
        rc = ehem_key_list_all(ctx, &keys);       /* auth happens HERE */
    }
    if (rc != EHEM_OK) {
        const ehem_error *e = ehem_last_error(ctx);
        fprintf(stderr, "failed: %s — %s (http %ld)\n",
                ehem_rc_str(rc), e->message, e->http_status);
    } else {
        for (i = 0; i < keys->listed; i++) {
            printf("%s  %s\n", keys->entries[i].kid,
                   keys->entries[i].label ? keys->entries[i].label : "");
        }
    }

    ehem_key_page_free(keys);              /* NULL-safe, like every _free */
    ehem_ctx_destroy(ctx);                 /* wipes credentials */
    ehem_global_cleanup();
    return rc == EHEM_OK ? 0 : 1;
}
```

Signing with a device key and verifying the result on the device:

```c
    ehem_signature *sig = NULL;
    rc = ehem_sign(ctx, kid_hex, msg, msg_len,
                   EHEM_SIGN_ALG_ED25519, NULL, 0, &sig);
    if (rc == EHEM_OK) {
        rc = ehem_verify(ctx, kid_hex, msg, msg_len, sig->sig, sig->len,
                         EHEM_SIGN_ALG_ED25519, NULL, 0);   /* EHEM_OK = valid */
    }
    ehem_signature_free(sig);
```

## Conventions

### Context and threading

One `ehem_ctx` (opaque) = one HEM device, created from a URL + options,
destroyed with `ehem_ctx_destroy()` (which zeroizes credential material).
There is no global mutable state beyond `ehem_global_init()` /
`ehem_global_cleanup()`, which wrap the backends' process-global setup
(libcurl, wolfCrypt) and are idempotent. In 1.x a context is used by one
thread at a time; internals keep all mutable state inside the context so a
per-context lock can be added later without redesign.

### Error model

Every fallible call returns `ehem_rc`. `EHEM_OK` is 0; the failure values
distinguish every condition a PKCS#11 backend must map: `EHEM_ERR_NETWORK`,
`EHEM_ERR_UNREACHABLE`, `EHEM_ERR_AUTH_EXPIRED`, `EHEM_ERR_AUTH_FAILED`,
`EHEM_ERR_SCOPE_DENIED`, `EHEM_ERR_USER_REJECTED`,
`EHEM_ERR_CONFIRM_TIMEOUT`, `EHEM_ERR_NOT_FOUND`, `EHEM_ERR_DEVICE`,
`EHEM_ERR_PROTOCOL`, `EHEM_ERR_ARG`, `EHEM_ERR_NOMEM`,
`EHEM_ERR_UNSUPPORTED`. The enum is append-only — values never renumber.
`ehem_rc_str()` names any value. Detail for the LAST call —
HTTP status, the device's error payload, a human-readable message — is
retrievable from the context via `ehem_last_error()`; the returned struct
is context-owned and valid until the next call on that context.

HTTP mapping (shared by all bindings): 401 → `EHEM_ERR_AUTH_FAILED`
(after one silent re-acquisition retry on scoped requests), 403 →
`EHEM_ERR_SCOPE_DENIED`, 404 → `EHEM_ERR_NOT_FOUND`, other 4xx/5xx →
`EHEM_ERR_DEVICE` with the payload preserved. Where firmware conflates
conditions under one status (e.g. crypto 406 = not-found OR wrong type OR
crypto failure), the SDK deliberately maps to `EHEM_ERR_DEVICE`, never
guessing — the headers note each case.

### Memory and ownership

Output structs are heap-allocated by the library and released with their
matching `ehem_*_free()` — all NULL-safe. Byte buffers are
`(uint8_t *ptr, size_t len)` pairs; strings are NUL-terminated UTF-8.
Secret material (passphrase-derived keys, shared secrets, plaintexts,
tokens) is zeroized on free. Strings returned by `ehem_version()` and
`ehem_rc_str()` are static — never free them.

### Options and the ABI

`ehem_options` uses a size/version discipline: `abi_size` is the first
field, stamped by `ehem_options_init()` — the ONLY supported way to
prepare the struct. The library reads only the fields the caller's
`abi_size` covers, and zero is always "use the default" for every scalar,
so the struct grows append-only across versions without breaking either
direction of the ABI. Response parsing is tolerant everywhere: unknown
JSON fields are ignored (device firmware may be newer than the SDK);
missing REQUIRED fields are `EHEM_ERR_PROTOCOL` with detail.

### Authentication: passphrase and mobile

Both logins are **lazy** — they store the mode; the network exchange runs
at the first call that needs a bearer, and tokens are cached per scope
with expiry-aware silent refresh.

- `ehem_login(ctx, passphrase)`: eJWT challenge–response (PBKDF2-SHA256
  600k, salt = the challenge `eid`; X25519 ECDH; HMAC-SHA256-signed JWT).
  `ehem_options.no_credential_retention` scrubs the passphrase after the
  first acquisition; `ehem_logout()` drops credentials + the token cache.
- `ehem_login_mobile(ctx)`: push confirmation on a paired phone (the
  `auth/ext` flow + the cloud notification broker). Each scope's
  acquisition blocks up to `ehem_options.confirm_timeout_ms` (default
  60 s) and fails with `EHEM_ERR_USER_REJECTED` or
  `EHEM_ERR_CONFIRM_TIMEOUT` — distinct by design. Pairing management
  itself always needs a passphrase login (the device demands `sub="U"`).
- The pollable confirm engine (`ehem_ext_confirm_begin/poll/wait/cancel`)
  serves consumers that cannot block.

### Scopes

Bearer tokens are scope-bound. System/logger/storage bindings use
free-form scopes (`system:config`, `logger:get`,
`storage:disk<N>[:rw]` — the storage scope CARRIES its arguments);
key-management list/search/create/delete/update/import use their family
scopes; **every per-key crypto operation uses the exact scope
`keymgmt:use:<kid>`** — firmware matches it with a plain strcmp, so one
cached token per KID serves get + sign + verify + ECDH + cipher + PQC on
that key. The cache handles all of this transparently.

### TLS trust

Three modes via `ehem_options.tls_mode`: `EHEM_TLS_SYSTEM` (default),
`EHEM_TLS_CA_FILE` (pinned CA via `ca_file`), `EHEM_TLS_INSECURE`
(explicit lab opt-in). Cloud legs (check-in relay, notification broker,
TLS recovery) are ALWAYS verified regardless of the mode — the relaxation
applies only to the device connection.

### Automatic recoveries

The SDK heals the known device pathologies with bounded, opt-out-able
retries (at most one each per request):

- **Expired device certificate** → check-in → single retry on a fresh
  connection (`no_auto_checkin` opts out; `ehem_cert_refreshed()` reports
  it happened).
- **Device clock drift / unset RTC** (the RTC gains ~8%/day-scale drift,
  see KNOWN-ISSUES) → drift-gated single check-in + retry on login 401 /
  challenge 403 / stale mobile authreq.
- **Proactive**: `checkin_on_login` runs one best-effort check-in before
  the first token acquisition — recommended for session-oriented
  consumers (a PKCS#11 module).
- 401 on a scoped request → invalidate + re-acquire + retry once.

Anything beyond these is the caller's policy: the SDK does no generic
retries (`request_pace_ms` offers optional client-side pacing for
rate-sensitive devices).

## Module index

### `<ehem/ehem.h>` — core

| Symbol | What it is |
|---|---|
| `EHEM_API` | export/visibility macro (define `EHEM_USING_SHARED` when consuming the DLL on Windows) |
| `ehem_version` | runtime SDK version string |
| `ehem_rc`, `ehem_rc_str` | the error enum (append-only) + name lookup |
| `ehem_ctx`, `ehem_ctx_create`, `ehem_ctx_destroy` | opaque per-device context lifecycle |
| `ehem_options`, `ehem_options_init` | context options; init stamps `abi_size` |
| `ehem_tls_mode` | `EHEM_TLS_SYSTEM` / `EHEM_TLS_CA_FILE` / `EHEM_TLS_INSECURE` |
| `ehem_error`, `ehem_last_error` | last-call detail (http status, device payload, message) |
| `ehem_cert_refreshed` | sticky flag: automatic cert recovery took effect |
| `ehem_global_init`, `ehem_global_cleanup` | idempotent process-global backend setup |
| `ehem_transport` | opaque transport-override slot in the options (testing seam) |
| `EHEM_DEFAULT_CONNECT_TIMEOUT_MS`, `EHEM_DEFAULT_TOTAL_TIMEOUT_MS`, `EHEM_DEFAULT_CONFIRM_TIMEOUT_MS` | documented option defaults |
| `EHEM_DEFAULT_CHECKIN_URL` | the Encedo cloud check-in endpoint |

### `<ehem/auth.h>` — sessions, ExtAuth, broker, confirm engine

| Symbol | What it is |
|---|---|
| `ehem_login`, `ehem_logout` | lazy passphrase session; logout scrubs credentials + cache |
| `ehem_login_mobile` | lazy mobile (push-confirm) session mode |
| `ehem_ext_init`, `ehem_ext_init_info`, `ehem_ext_init_free` | begin pairing: device emits the request JWT |
| `ehem_ext_validate`, `ehem_ext_validate_info`, `ehem_ext_validate_free` | finalise pairing (imports the authenticator key) |
| `ehem_ext_mac`, `ehem_ext_mac_info`, `ehem_ext_mac_free` | stateless device liveness/identity proof |
| `ehem_ext_request`, `ehem_ext_request_info`, `ehem_ext_request_free` | unauthenticated: emit an `authreq` for paired phones |
| `ehem_ext_token`, `ehem_ext_token_free` | exchange the phone's `authreply` for a bearer (the one public token return) |
| `ehem_notify_session`, `ehem_notify_register_init`, `ehem_notify_register_info`, `ehem_notify_register_info_free` | broker session + pairing registration legs |
| `ehem_notify_register_check`, `ehem_notify_pairing_reply`, `ehem_notify_pairing_reply_free`, `ehem_notify_register_finalise` | pairing scan poll + completion |
| `ehem_notify_event_new`, `ehem_notify_event_check`, `ehem_notify_event_result`, `ehem_notify_event_result_free` | login push + answer poll |
| `ehem_notify_string_free` | free helper for broker strings |
| `ehem_ext_confirm_begin`, `ehem_ext_confirm_poll`, `ehem_ext_confirm_wait`, `ehem_ext_confirm_cancel` | pollable confirm engine (+ blocking wrapper) |
| `ehem_confirm_status` | pending / approved / rejected / expired |
| `EHEM_DEFAULT_NOTIFY_URL` | the cloud notification-broker base |

### `<ehem/keymgmt.h>` — key management

| Symbol | What it is |
|---|---|
| `ehem_key_list`, `ehem_key_list_all`, `ehem_key_page`, `ehem_key_entry`, `ehem_key_page_free` | one page / full walk of the key inventory |
| `ehem_key_search`, `ehem_key_search_all`, `ehem_key_search_mode` | DESCR pattern search (prefix/suffix/substring; no-match = empty page) |
| `ehem_key_get`, `ehem_key_details`, `ehem_key_details_free` | public material + metadata by KID |
| `ehem_key_create`, `ehem_key_create_params` | generate a key on the device |
| `ehem_key_delete` | remove a key by KID |
| `ehem_key_update` | rewrite label/descr (the device replaces the WHOLE record) |
| `ehem_key_import`, `ehem_key_import_params` | import an external public key |
| `ehem_key_derive` | ECDH+HKDF-derive a new stored key (device-side-only reproducibility) |
| `ehem_key_type_parse`, `ehem_key_type_info`, `ehem_key_family`, `ehem_key_family_str` | client-side classifier for device type strings |
| `EHEM_KID_HEX_SIZE` | KID hex-string buffer size (32 chars + NUL) |
| `EHEM_KEY_MODE_ECDH`, `EHEM_KEY_MODE_EXDSA` | key-mode bit flags |
| `EHEM_KEY_ROLE_ATT`, `EHEM_KEY_ROLE_PKEY`, `EHEM_KEY_ROLE_CERT`, `EHEM_KEY_ROLE_GENERIC_DER` | role bit flags |

### `<ehem/crypto.h>` — cryptographic operations (all per-KID)

| Symbol | What it is |
|---|---|
| `ehem_sign`, `ehem_signature`, `ehem_signature_free` | ECDSA (DER) / EdDSA (raw) signatures |
| `ehem_verify` | device-side verification (`EHEM_OK` = valid) |
| `ehem_ecdh`, `ehem_ecdh_secret`, `ehem_ecdh_secret_free` | ECDH shared secret (raw mode truncates to 32 B on fw v1.2.2 — use hashed algs for >256-bit curves) |
| `ehem_hmac`, `ehem_mac`, `ehem_mac_free`, `ehem_hmac_verify` | HMAC by stored or ECDH-derived key |
| `ehem_encrypt`, `ehem_ciphertext`, `ehem_ciphertext_free` | AES encrypt (IV always device-generated) |
| `ehem_decrypt`, `ehem_plaintext`, `ehem_plaintext_free` | AES decrypt (plaintext zeroized on free) |
| `ehem_wrap`, `ehem_wrapped`, `ehem_wrapped_free` | AES key wrap (RFC 3394/5649 style) |
| `ehem_unwrap`, `ehem_unwrapped`, `ehem_unwrapped_free` | AES key unwrap (output zeroized on free) |
| `ehem_mlkem_encaps`, `ehem_mlkem_encaps_result`, `ehem_mlkem_encaps_result_free` | ML-KEM encapsulate (ss zeroized) |
| `ehem_mlkem_decaps`, `ehem_mlkem_secret`, `ehem_mlkem_secret_free` | ML-KEM decapsulate |
| `ehem_mldsa_sign`, `ehem_mldsa_signature`, `ehem_mldsa_signature_free` | ML-DSA signatures |
| `ehem_mldsa_verify` | ML-DSA verification |
| `ehem_random` | device hardware RNG (harvested from AES-CBC IVs; needs a caller-designated AES key) |
| `EHEM_SIGN_ALG_ED25519`, `EHEM_SIGN_ALG_ED25519CTX`, `EHEM_SIGN_ALG_ED25519PH`, `EHEM_SIGN_ALG_ED448`, `EHEM_SIGN_ALG_ED448PH`, `EHEM_SIGN_ALG_SHA256_ECDSA`, `EHEM_SIGN_ALG_SHA384_ECDSA`, `EHEM_SIGN_ALG_SHA512_ECDSA` | ExDSA selectors |
| `EHEM_HASH_ALG_SHA2_256`, `EHEM_HASH_ALG_SHA2_384`, `EHEM_HASH_ALG_SHA2_512`, `EHEM_HASH_ALG_SHA3_256`, `EHEM_HASH_ALG_SHA3_384`, `EHEM_HASH_ALG_SHA3_512` | hash selectors (ecdh/hmac) |
| `EHEM_CIPHER_ALG_AES128_ECB`, `EHEM_CIPHER_ALG_AES128_CBC`, `EHEM_CIPHER_ALG_AES128_GCM`, `EHEM_CIPHER_ALG_AES192_ECB`, `EHEM_CIPHER_ALG_AES192_CBC`, `EHEM_CIPHER_ALG_AES192_GCM`, `EHEM_CIPHER_ALG_AES256_ECB`, `EHEM_CIPHER_ALG_AES256_CBC`, `EHEM_CIPHER_ALG_AES256_GCM` | cipher selectors (exactly 10 chars each, device-enforced) |
| `EHEM_SIGN_MSG_MAX`, `EHEM_SIGN_SIG_CTX_MAX`, `EHEM_VERIFY_SIG_MAX`, `EHEM_ECDH_PUBKEY_MAX`, `EHEM_HMAC_MAC_MAX`, `EHEM_CIPHER_MSG_MAX`, `EHEM_CIPHER_CT_MAX`, `EHEM_CIPHER_IV_LEN`, `EHEM_CIPHER_TAG_LEN`, `EHEM_CIPHER_AAD_MAX`, `EHEM_CIPHER_HKDF_CTX_MAX`, `EHEM_WRAP_MSG_MAX`, `EHEM_WRAP_IV_LEN`, `EHEM_MLKEM_CT_MAX`, `EHEM_MLKEM_SS_LEN`, `EHEM_MLDSA_SIG_MAX`, `EHEM_PQC_ALG_SIZE` | wire-limit constants (device-enforced values pinned from firmware) |

### `<ehem/system.h>` — system, check-in, TLS lifecycle

| Symbol | What it is |
|---|---|
| `ehem_system_status`, `ehem_status_info`, `ehem_system_status_free` | device status (no auth) |
| `ehem_system_version`, `ehem_version_info`, `ehem_system_version_free` | hw/fw/bootloader identity (bootloader triple conditional) |
| `ehem_system_checkin`, `ehem_checkin_info`, `ehem_checkin_result_free` | 3-leg cloud check-in relay (resyncs the device clock; harvests the delivered cert chain + current serial) |
| `ehem_cert_inspect`, `ehem_cert_info`, `ehem_cert_info_free` | read a base64-DER chain's leaf identity |
| `ehem_system_config`, `ehem_config_info`, `ehem_system_config_free` | typed config read |
| `ehem_system_config_install_cert`, `ehem_cert_install_info`, `ehem_cert_install_free` | install a TLS certificate (reboot required to load it) |
| `ehem_tls_recover`, `EHEM_DEFAULT_REGISTER_URL` | full TLS restoration via the provisioning cloud (post-wipe) |
| `ehem_system_reboot` | reboot (drops the token cache) |
| `ehem_system_selftest`, `ehem_selftest_info`, `ehem_selftest_free` | self-test battery + key-repo statistics |
| `ehem_system_attestation`, `ehem_attestation_info`, `ehem_attestation_free` | ATECC attestation material (PPA builds) |
| `ehem_system_shutdown` | stop network/USB — recovery is a PHYSICAL power-cycle |

### `<ehem/logger.h>` — audit log (PPA builds)

| Symbol | What it is |
|---|---|
| `ehem_logger_key`, `ehem_logger_key_info`, `ehem_logger_key_free` | Ed25519 log-signing key + signed nonce |
| `ehem_logger_list`, `ehem_logger_page`, `ehem_logger_page_free` | log-file id listing |
| `ehem_logger_get`, `ehem_logger_file_free` | raw log-file download (pipe-delimited records) |
| `EHEM_LOGGER_KEY_SIZE`, `EHEM_LOGGER_NONCE_SIZE`, `EHEM_LOGGER_SIG_SIZE` | fixed field sizes |

### `<ehem/storage.h>` — USB mass-storage visibility

| Symbol | What it is |
|---|---|
| `ehem_storage_unlock` | expose disk N over USB-MSC (the SCOPE carries disk + rw) |
| `ehem_storage_lock` | hide disk N again |

## hem-tool

The CLI is a thin consumer of exactly this public API and doubles as its
living documentation: `hem-tool help <command>` documents each command
(auth requirement included), the top-level page groups commands by
credential need, and every command accepts `--mobile` where a bearer
suffices. Default device URL: `https://my.ence.do`.
