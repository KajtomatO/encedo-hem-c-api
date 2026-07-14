# Requirements — Encedo HEM backend (`hem.c`) of encedo-pkcs11

## 0. Scope and conventions

encedo-pkcs11 is an MIT-licensed PKCS#11 (Cryptoki) v2.40 module that
exposes the Encedo HEM device to PKCS#11 applications (OpenSSL, NSS,
OpenSSH, p11-kit, HashiCorp Vault, ...). It has two layers:

- an **interface layer** implementing all `C_*` entry points — complete,
  out of scope for this document;
- a **backend**, `hem.c`, which is the only component that talks to the
  device and the only file that grows during backend work.

The seam between the two is the set of `hem_*` functions declared in
`hem.h`. **Function signatures and per-function API contracts live in
the comments of `hem.h` and are deliberately not repeated here** — read
`hem.h` first; this document adds the behavioral, data-model, and
policy requirements that the code files do not state.

Requirement keywords **SHALL** (mandatory), **SHOULD** (strong
recommendation), **MAY** (optional) are used per RFC 2119. Each
requirement has a stable ID (`HEM-<AREA>-<n>`); IDs are never reused.

## 1. Background: the HEM device model

The requirements below rest on these facts about the HEM:

- Every key has a unique, immutable 16-byte **KID**.
- A key carries a **LABEL** (ASCII, user description) and a **DESCR**
  (max 64 bytes, opaque blob, mutable during key life, server-side
  searchable by prefix match on the first N characters).
- The HEM search API returns per key: KID, key type, allowed modes
  (ExDSA / ECDH / both), creation date, last-update date.
- A HEM instance is addressed by **URL**.
- Base authentication is a **passphrase** or a **mobile-app
  challenge** (push notification confirmed on a phone).
- Every key operation additionally requires a per-KID **scope token**
  with a TTL configured on the HEM side.
- Key types provided by the HEM: ECDSA/ECDH on secp256r1, secp384r1,
  secp521r1, secp256k1; Ed25519/Ed448; X25519/X448 (ECDH);
  AES-128/192/256; HMAC with SHA-2 and SHA-3; ML-KEM 512/768/1024;
  ML-DSA 44/65/87.

## 2. General (HEM-GEN)

**HEM-GEN-1** `hem.c` SHALL be implemented on top of the dedicated
Encedo HEM C SDK. It SHALL NOT be modeled on, or talk directly to, the
HEM HTTP API.

**HEM-GEN-2** All SDK types, handles, and connection state SHALL be
confined to `hem.c` as static state: created in `hem_connect`, freed in
`hem_disconnect`, never visible above the `hem_*` seam. The SDK SHALL
be linked in the module's build system only; no other source file gains
an SDK include or type.

**HEM-GEN-3** All code SHALL be written from scratch under the MIT
license. Code SHALL NOT be copied from other PKCS#11 implementations
(most are GPL-licensed, including their headers).

**HEM-GEN-4** Until a given feature is implemented, the existing
graceful-placeholder behavior SHALL be preserved: `hem_connect` returns
`CKR_OK`, object enumeration reports an empty token, and
`hem_get_token_info` returns `CKR_FUNCTION_NOT_SUPPORTED` (which makes
the interface layer fall back to static token data). The module must
always load cleanly in real applications.

**HEM-GEN-5** `hem.c` MAY assume single-threaded access in 1.x. State
SHALL be organized so that serialization (a lock around the seam) can
be added later without redesign.

## 3. Slots and configuration (HEM-CFG)

**HEM-CFG-1** One HEM URL = one slot = one token. Keys SHALL be
represented as objects, never as slots.

**HEM-CFG-2** Configuration SHALL be read exactly once, during
`hem_connect` (i.e. at `C_Initialize`), with this resolution order
(first hit wins):

1. `$ENCEDO_PKCS11_CONF` — explicit config-file path;
2. per-user path: Linux `$XDG_CONFIG_HOME/encedo-pkcs11.conf`
   (default `~/.config/encedo-pkcs11.conf`), macOS
   `~/Library/Application Support/encedo-pkcs11.conf`, Windows
   `%APPDATA%\encedo-pkcs11.conf`;
3. system path: `/etc/encedo-pkcs11.conf` (Windows:
   `%PROGRAMDATA%\encedo-pkcs11.conf`);
4. `$ENCEDO_PKCS11_URL` — single-HEM shortcut, no file needed;
5. nothing found → initialization succeeds with **zero slots** (clean
   diagnostics, not an error).

**HEM-CFG-3** The file format SHALL be flat INI-style text, parsed by a
small dependency-free parser (~50 lines; no JSON). Recognized keys per
`[hem <name>]` section: `url`, `auth` (`passphrase` | `mobile`),
`confirm_timeout` (seconds to wait for a mobile confirmation), `slot`
(optional pinned slot id). Example:

```ini
[hem work]
url = https://hem1.example.com
auth = mobile
confirm_timeout = 60

[hem personal]
slot = 5
url = https://hem2.example.com
auth = passphrase
```

**HEM-CFG-4** Slot ids SHALL default to section order (1, 2, ...);
`slot = N` pins a section to a fixed id. Removing a section SHALL NOT
renumber the remaining ones (applications such as Vault hardcode slot
numbers in their own configuration).

**HEM-CFG-5** The configuration file SHALL contain no secrets. The
passphrase arrives only as the PIN via `C_Login`, never from disk.
Configuration changes take effect on application restart only (read
once — see HEM-CFG-2).

**HEM-CFG-6** If the configured HEM is unreachable, the slot SHALL
remain present with the token reported absent (operations on it return
`CKR_TOKEN_NOT_PRESENT`).

## 4. Object model (HEM-OBJ)

**HEM-OBJ-1** `hem.c` SHALL own a `CK_OBJECT_HANDLE ↔ (KID, object
class)` mapping table. In addition to the handle rules stated in
`hem.h`, handles SHALL never be reused after object destruction. One
HEM key pair SHALL yield two objects — `CKO_PRIVATE_KEY` and
`CKO_PUBLIC_KEY` — i.e. two handles sharing one KID. KID is the
identity; DESCR is not (it is mutable).

**HEM-OBJ-2** Attribute identity mapping: `CKA_ID` SHALL default to the
16-byte KID; `CKA_LABEL` SHALL map 1:1 to the HEM LABEL.

**HEM-OBJ-3** DESCR SHALL be wrapper-owned metadata in the format

```
PKCS11:<v>:<app-cka-id>
```

where `PKCS11:` is a fixed marker (one prefix query finds exactly the
wrapper-managed objects and nothing else stored on the HEM); `<v>` is a
one-character format version (DESCR is mutable, so migration is a
rewrite, but old versions must stay recognizable); `<app-cka-id>` is
present only when an application supplied its own `CKA_ID` in a
creation template (NSS does; its SHA-1-derived IDs fit the ~55 free
bytes). When present, that value SHALL be reported as `CKA_ID` instead
of the KID. DESCR SHALL NOT duplicate anything the search API already
returns (KID, type, modes, dates).

**HEM-OBJ-4** Object-class mapping: key pair → private + public
objects; public-only key → `CKO_PUBLIC_KEY`; raw AES/HMAC key →
`CKO_SECRET_KEY` with `CKA_SENSITIVE=CK_TRUE` and
`CKA_EXTRACTABLE=CK_FALSE` always.

**HEM-OBJ-5** Attributes SHALL be sourced from a HEM search record as
follows:

| search result field | PKCS#11 attribute(s) |
|---|---|
| KID | `CKA_ID` (unless overridden per HEM-OBJ-3) |
| key type (SECP256R1, ED25519, AES256, ...) | `CKA_KEY_TYPE`, `CKA_EC_PARAMS` (curve OID) |
| mode ExDSA | `CKA_SIGN` (private) / `CKA_VERIFY` (public) |
| mode ECDH | `CKA_DERIVE` |
| pair / public-only / raw | object class(es) |
| LABEL | `CKA_LABEL` |
| created / updated dates | ignored (no standard key attribute; vendor attribute MAY be added later) |

**HEM-OBJ-6** Certificates (`CKO_CERTIFICATE`) are out of scope for 1.x
(planned for the 2.0 milestone, last on the roadmap). The DESCR format
above already reserves room for them; nothing in the object model may
preclude adding them.

## 5. Object search (HEM-FIND)

**HEM-FIND-1** A find operation SHALL issue exactly **one** HEM prefix
search (`PKCS11:` on DESCR); all further filtering (object class,
`CKA_KEY_TYPE`, `CKA_SIGN`/`CKA_DERIVE`, `CKA_ID`, `CKA_LABEL`, ...)
SHALL happen in the wrapper. (Right trade-off at HEM scale: tens of
keys, not thousands.)

**HEM-FIND-2** The search result SHALL be cached for the duration of
one `hem_find_objects_init` → `hem_find_objects` →
`hem_find_objects_final` iteration, giving the snapshot semantics the
PKCS#11 spec expects.

**HEM-FIND-3** If the HEM instance requires authorization for search, a
pre-login search SHALL yield an empty list (and the token advertises
`CKF_LOGIN_REQUIRED`, so applications log in and repeat the search). If
the HEM allows anonymous search, objects SHALL be visible in a public
session.

## 6. Authentication (HEM-AUTH)

Two layers, both fully hidden inside `hem.c`.

**HEM-AUTH-1** In `auth = passphrase` mode, the PIN passed to
`hem_login` **is** the HEM passphrase.

**HEM-AUTH-2** In `auth = mobile` mode, the token SHALL advertise
`CKF_PROTECTED_AUTHENTICATION_PATH`; `hem_login` is then called with a
NULL PIN, SHALL fire the mobile challenge, and SHALL block until
confirmation or `confirm_timeout` (same UX as pinpad readers).

**HEM-AUTH-3** `CKU_SO` SHALL NOT be supported (no HEM admin-role
mapping for now).

**HEM-AUTH-4** `hem.c` SHALL cache scope tokens as
`KID → (scope_token, expiry)`.

**HEM-AUTH-5** A scope token SHALL be acquired lazily at the **first
real operation** on a key — never at operation init (`C_SignInit`
etc.), and never for a length query.

**HEM-AUTH-6** Cached scope tokens SHALL be refreshed silently before
expiry (with a small safety margin). In passphrase mode acquisition is
a silent request (milliseconds; the user sees no prompt after login).
In mobile mode each acquisition is a push notification (first use of a
key, and again after TTL expiry).

**HEM-AUTH-7** `hem_logout` SHALL drop the base credential **and the
entire scope-token cache**.

**HEM-AUTH-8** The scope TTL is HEM-side policy: an administrator
setting TTL≈0 enforces confirm-every-operation. The wrapper SHALL NOT
implement `CKA_ALWAYS_AUTHENTICATE` machinery of its own.

## 7. Cryptographic operations (HEM-OP)

**HEM-OP-1** All operations are single-part: each `hem_*` operation
call receives the complete input in one call. (The interface layer
already rejects multi-part, digest, and wrap/unwrap APIs; hashing is
done by the client application — the HEM is a network signing device.)

**HEM-OP-2** Length queries SHALL be answered locally from a
key-type → output-size table and SHALL never touch the device (e.g.
ECDSA P-256 = 64 B, Ed25519 = 64 B, ML-DSA-65 = 3309 B). This makes the
PKCS#11 two-call size convention cost zero network round-trips and zero
mobile pushes.

**HEM-OP-3** Operation coverage SHALL be: sign/verify — ECDSA (all four
curves of §1), Ed25519/Ed448, ML-DSA 44/65/87, HMAC (SHA-2/SHA-3);
derive — ECDH on all supported curves including X25519/X448;
encrypt/decrypt — AES-128/192/256; key generation — all of the above
plus ML-KEM; random — hardware RNG.

**HEM-OP-4** ML-KEM encapsulate/decapsulate SHALL be implemented behind
the existing seam functions; they become application-visible only once
the PKCS#11 3.2 interface (`C_EncapsulateKey`/`C_DecapsulateKey`) is
added to the interface layer.

## 8. Error mapping (HEM-ERR)

**HEM-ERR-1** Backend events SHALL map to PKCS#11 return values exactly
as follows:

| event | returned to the application |
|---|---|
| user confirmed on phone | `CKR_OK` + result |
| user rejected on phone | `CKR_FUNCTION_CANCELED` |
| push timeout (`confirm_timeout`) | `CKR_FUNCTION_CANCELED` |
| base credential expired, refresh impossible | `CKR_USER_NOT_LOGGED_IN` (applications re-run `C_Login`) |
| scope denied for this KID | `CKR_KEY_FUNCTION_NOT_PERMITTED` |
| network failure mid-operation | `CKR_DEVICE_ERROR` |
| HEM unreachable | slot present, token absent (`CKR_TOKEN_NOT_PRESENT`) |

## 9. Requirements on the Encedo HEM C SDK (HEM-SDK)

What `hem.c` needs **from** the SDK to satisfy the requirements above.
Input for the SDK design; each item cites the requirement it serves.

**HEM-SDK-1** Open/close a connection context for a HEM instance
addressed by URL, with a reachability check usable for token-presence
reporting. (HEM-GEN-2, HEM-CFG-6)

**HEM-SDK-2** Base authentication by passphrase, and by mobile
challenge with a blocking or pollable wait and a caller-supplied
timeout. (HEM-AUTH-1, HEM-AUTH-2)

**HEM-SDK-3** Scope-token acquisition per KID, returning the token and
its TTL/expiry so the cache can refresh proactively; operations accept
a caller-provided scope token. (HEM-AUTH-4..6)

**HEM-SDK-4** Key search by DESCR prefix returning, per key: KID, key
type, allowed modes (ExDSA/ECDH/both), LABEL, DESCR, creation and
last-update dates. (HEM-FIND-1, HEM-OBJ-5)

**HEM-SDK-5** Read public key material by KID (needed to serve
`CKA_VALUE`/`CKA_EC_POINT`-style queries on public objects).
(HEM-OBJ-5)

**HEM-SDK-6** Key management by KID: generate (all types of §1),
delete, update LABEL, update DESCR. (HEM-OBJ-1..3)

**HEM-SDK-7** Single-shot operations, all keyed by KID + scope token:
sign, verify, ECDH derive, AES encrypt/decrypt (including GCM IV and
tag handling), ML-KEM encapsulate/decapsulate, and hardware random.
(HEM-OP-1..4)

**HEM-SDK-8** Distinguishable error conditions: user rejection vs.
confirmation timeout vs. expired base credential vs. scope denied vs.
network failure vs. device unreachable — required to implement the
HEM-ERR-1 table faithfully.

**HEM-SDK-9** The login response SHOULD be able to carry an initial (or
session) scope, so the mobile flow needs one push instead of two on the
common login-then-sign path. (open design question on the HEM side)

## 10. Implementation order / milestones

Bring-up order chosen so each step is exercised by real applications as
early as possible. Certificates stay last (2.0).

| milestone | delivers | requirements |
|---|---|---|
| M1 — connection & config | config parsing, `hem_connect`/`hem_disconnect`, reachability | HEM-GEN-1..2, HEM-CFG-1..6 |
| M2 — token info | live `hem_get_token_info` (label, serial, flags, PIN limits) | HEM-GEN-4 retired for this call |
| M3 — login | `hem_login`/`hem_logout`, both auth modes | HEM-AUTH-1..3, HEM-AUTH-7 |
| M4 — objects visible | handle↔KID table, `hem_find_objects*`, `hem_get_attribute_value` | HEM-OBJ-1..5, HEM-FIND-1..3 |
| M5 — first signature | `hem_sign` (+ scope-token cache), local length table | HEM-AUTH-4..6, HEM-OP-1..2, HEM-ERR-1 |
| M6 — key management | `hem_generate_key_pair`, `hem_generate_key`, `hem_destroy_object`, `hem_set_attribute_value`, `hem_generate_random` | HEM-SDK-6, HEM-OP-3 |
| M7 — remaining ops | `hem_verify`, `hem_derive_key`, `hem_encrypt`/`hem_decrypt` | HEM-OP-3 |
| M8 — PKCS#11 3.2 | `hem_encapsulate`/`hem_decapsulate` exposed via 3.2 interface | HEM-OP-4 |
| 2.0 — certificates | `CKO_CERTIFICATE` (needs HEM-side support) | HEM-OBJ-6 |
