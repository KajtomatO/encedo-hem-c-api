/*
 * keymgmt.h — Encedo HEM C SDK, key-management protocol bindings.
 *
 * implements: REQ-KEY-001, REQ-API-005
 *
 * The key repository is the M3 foundation (ARCHITECTURE.md §6, §11): every key
 * the device holds — including its own TLS material and paired-authenticator
 * public keys — is enumerable here; filtering/protection is the caller's job
 * (REQ-TOOL-005). This header covers the paginated inventory:
 *
 *   - ehem_key_list()      one page over GET /api/keymgmt/list[/{offset}[/{limit}]]
 *   - ehem_key_list_all()  the whole repository, walked into one merged page
 *
 * Both parse the device's `{offset, total, listed, list:[…]}` envelope into a
 * caller-owned ehem_key_page (tolerant parsing, ARCHITECTURE.md §6). Search
 * (REQ-KEY-002) returns the identical envelope and reuses these structs.
 */
#ifndef EHEM_KEYMGMT_H
#define EHEM_KEYMGMT_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One key's metadata, as carried by a list/search entry. `kid` and `type` are
 * always present (a missing one is EHEM_ERR_PROTOCOL); the rest are optional —
 * an absent string is NULL, an absent/empty `descr` is NULL with descr_len 0.
 * All memory is owned by the enclosing ehem_key_page and released by
 * ehem_key_page_free(); no field is freed individually.
 */
typedef struct ehem_key_entry {
    char    *kid;        /* 32-char hex key id (wire format preserved) */
    char    *type;       /* algorithm string as sent by the device, e.g. "ED25519" */
    char    *label;      /* human label as stored (NULL when absent) */

    /* Opaque per-key blob (base64-decoded from the wire `descr`). The device
     * omits the field when empty, so descr is NULL / descr_len 0 in that case. */
    uint8_t *descr;
    size_t   descr_len;

    int64_t  created;    /* creation time, unix seconds (0 if the device omits it) */
    int64_t  updated;    /* last-update time, unix seconds (0 if omitted) */
} ehem_key_entry;

/*
 * One page of a listing (ehem_key_list) or the whole repository merged into a
 * single page (ehem_key_list_all). Caller-owned; release with
 * ehem_key_page_free().
 */
typedef struct ehem_key_page {
    int64_t         offset;   /* effective offset the device applied (echo) */
    int64_t         total;    /* total number of keys in the repository */
    size_t          listed;   /* number of entries below (the wire `listed`) */
    ehem_key_entry *entries;  /* `listed` entries; NULL exactly when listed == 0 */
} ehem_key_page;

/*
 * Fetch ONE page of the key inventory: GET /api/keymgmt/list[/{offset}[/{limit}]]
 * (scope "keymgmt:list"). `offset` skips that many entries (0 = from the start);
 * `limit` requests at most that many entries (0 = the device default). The
 * device hard-caps the effective limit at 15 regardless of what is asked, so a
 * caller wanting the whole repository should use ehem_key_list_all().
 *
 * On success writes *out (caller frees with ehem_key_page_free) and returns
 * EHEM_OK; otherwise returns an ehem_rc, leaves *out NULL, and records detail on
 * the context. A missing `kid`/`type` in any entry is EHEM_ERR_PROTOCOL; 401/403
 * map per REQ-AUTH-003; a device error (e.g. 406/409) is EHEM_ERR_DEVICE with the
 * device payload available via ehem_last_error().
 */
EHEM_API ehem_rc ehem_key_list(ehem_ctx *ctx, size_t offset, size_t limit,
                               ehem_key_page **out);

/*
 * Walk the ENTIRE key repository and return every key merged into one page
 * (out->offset 0, out->total the device's reported total, out->listed the number
 * of entries gathered). Pages internally with the "offset >= total (or a page of
 * zero entries) terminates" rule — never "fewer entries than requested", which
 * is unreliable given the server-side 15-item cap (REQ-KEY-001; python OQ-17).
 *
 * Same ownership and error contract as ehem_key_list(). On any page failure the
 * partial result is released and *out is left NULL.
 */
EHEM_API ehem_rc ehem_key_list_all(ehem_ctx *ctx, ehem_key_page **out);

/* Release a key page from ehem_key_list()/ehem_key_list_all(). NULL is a no-op. */
EHEM_API void ehem_key_page_free(ehem_key_page *page);

/* ==========================================================================
 * Key creation + deletion (authenticated).
 * implements: REQ-KEY-005 (create, scope "keymgmt:gen"),
 *             REQ-KEY-004 (delete, scope "keymgmt:del")
 * ========================================================================== */

/* Buffer size — including the NUL — an ehem_key_create() caller must provide for
 * the returned key id (16 bytes rendered as 32 hex chars). */
#define EHEM_KID_HEX_SIZE 33

/*
 * Parameters for ehem_key_create(). `type` and `label` are required; `mode` and
 * `descr` are optional (NULL / 0 to omit). The SDK keeps no allowlist and no
 * default: every field is passed through as given and the device is the
 * authority (an unsupported type or invalid mode is a device 400 with payload).
 */
typedef struct ehem_key_create_params {
    /* Algorithm literal, per encedo-hem-api-doc keymgmt/create.md — e.g.
     * "ED25519", "SECP256R1", "CURVE25519", "SHA2-256", "AES256", "MLKEM768".
     * (Note the device vocabulary: AES has no dash; HMAC uses the raw hash name.) */
    const char    *type;

    /* Human label, pre-validated client-side: printable ASCII, 1..32 bytes
     * (the device max, live-probed 2026-07-16 — 32 accepted, 33 → 400). A
     * label outside this bound is EHEM_ERR_ARG with no I/O. */
    const char    *label;

    /* Optional role mode, only meaningful for NIST-P ECC: one of "ECDH",
     * "ExDSA", or "ECDH,ExDSA". The device defaults NIST-P keys to ECDH-only,
     * so a signing key must pass a mode containing "ExDSA" (python OQ-19). NULL
     * omits the field (the SDK imposes no default). */
    const char    *mode;

    /* Optional opaque blob stored with the key; the SDK base64-encodes these raw
     * bytes for the wire. NULL / descr_len 0 omits the field. Capped at 64
     * bytes (the device's readback buffer size — a longer descr is silently
     * truncated on read, so the SDK refuses it): descr_len > 64 is
     * EHEM_ERR_ARG with no I/O. */
    const uint8_t *descr;
    size_t         descr_len;
} ehem_key_create_params;

/*
 * Generate a key on the device: POST /api/keymgmt/create (scope "keymgmt:gen").
 * On success writes the new key id (32 hex chars + NUL) into `kid_out` — a
 * caller-provided buffer of at least EHEM_KID_HEX_SIZE bytes — and returns
 * EHEM_OK. Returns EHEM_ERR_ARG (no network I/O) on a NULL argument, a missing
 * type/label, or a label that is not 1..31 printable-ASCII bytes; a device
 * validation failure is HTTP 400 and a full/failed repo write is HTTP 406, both
 * EHEM_ERR_DEVICE with the device payload in ehem_last_error(); 401/403 map per
 * REQ-AUTH-003. The returned kid is suitable for ehem_key_delete() and the
 * per-key crypto scopes.
 */
EHEM_API ehem_rc ehem_key_create(ehem_ctx *ctx,
                                 const ehem_key_create_params *params,
                                 char *kid_out);

/*
 * Delete a key: DELETE /api/keymgmt/delete/{kid} (scope "keymgmt:del"). `kid`
 * is validated client-side (exactly 32 hex chars, else EHEM_ERR_ARG with no
 * network I/O). Deletion is immediate and irreversible. Returns EHEM_OK on the
 * device's empty 200; a kid the device does not hold is HTTP 406 →
 * EHEM_ERR_NOT_FOUND; 401/403 map per REQ-AUTH-003. The SDK applies no
 * protected-key policy — that safeguard lives in hem-tool (REQ-TOOL-005/006).
 */
EHEM_API ehem_rc ehem_key_delete(ehem_ctx *ctx, const char *kid);

/* ==========================================================================
 * Key search by descr pattern (authenticated, scope "keymgmt:search").
 * implements: REQ-KEY-002
 * ========================================================================== */

/*
 * How a search pattern matches against a key's stored `descr` blob. The device
 * selects the mode from the raw query's shape (a leading '^', a trailing '$', or
 * neither); this enum is the SDK's typed spelling — the caller passes raw bytes
 * and the SDK forms the base64 + anchor itself.
 */
typedef enum ehem_key_search_mode {
    EHEM_KEY_SEARCH_SUBSTRING = 0,  /* descr contains the pattern (no anchor) */
    EHEM_KEY_SEARCH_PREFIX,         /* descr starts with the pattern ('^')    */
    EHEM_KEY_SEARCH_SUFFIX          /* descr ends with the pattern ('$')      */
} ehem_key_search_mode;

/*
 * Search the repository by `descr` pattern: POST /api/keymgmt/search (scope
 * "keymgmt:search"). `pattern`/`pattern_len` are RAW bytes (the SDK base64-encodes
 * them and prepends/appends the '^'/'$' anchor for `mode`); `pattern` may be NULL
 * only when `pattern_len` is 0. `offset`/`limit` page the results (sent verbatim;
 * the device caps the effective limit at 15). The result is the same
 * caller-owned ehem_key_page as ehem_key_list() (free with ehem_key_page_free).
 *
 * Returns EHEM_OK with a populated (or, when nothing matched, EMPTY) page. A
 * device "no keys matched" is HTTP 404 → EHEM_OK with an empty page (offset 0,
 * total 0, listed 0). EHEM_ERR_ARG (no I/O) on a NULL ctx/out, an invalid mode,
 * or a NULL pattern with pattern_len > 0; 400/406/410 are EHEM_ERR_DEVICE with
 * the device payload in ehem_last_error(); 401/403 map per REQ-AUTH-003.
 */
EHEM_API ehem_rc ehem_key_search(ehem_ctx *ctx,
                                 const uint8_t *pattern, size_t pattern_len,
                                 ehem_key_search_mode mode,
                                 size_t offset, size_t limit,
                                 ehem_key_page **out);

/*
 * Walk EVERY match for `pattern`/`mode` and return them merged into one page
 * (out->offset 0, out->total the device's reported total). Same pagination rule
 * and ownership/error contract as ehem_key_list_all(); a no-match search yields
 * an empty page (EHEM_OK).
 */
EHEM_API ehem_rc ehem_key_search_all(ehem_ctx *ctx,
                                     const uint8_t *pattern, size_t pattern_len,
                                     ehem_key_search_mode mode,
                                     ehem_key_page **out);

/* ==========================================================================
 * Single-key read (authenticated, per-key scope "keymgmt:use:<kid>").
 * implements: REQ-KEY-003
 * ========================================================================== */

/*
 * Public material + metadata for one key, as read by ehem_key_get(). `type` and
 * `updated` are always set. At most ONE material field is populated, decoded
 * from its base64 wire form:
 *   - pubkey : asymmetric keys — the raw public key in the algorithm-native
 *              encoding (e.g. 32 bytes for ED25519 / CURVE25519);
 *   - der    : types "CERT" / "DER_PKEY" — the DER bytes;
 *   - neither: symmetric keys (AES/HMAC) export no material.
 * `descr` is optional (NULL/0 when the key has none). `label` is intentionally
 * absent — the get endpoint never returns it (use list/search). All memory is
 * owned by the struct and released by ehem_key_details_free().
 */
typedef struct ehem_key_details {
    char    *type;        /* algorithm string as sent by the device */
    int64_t  updated;     /* last-update time, unix seconds */

    uint8_t *pubkey;      /* asymmetric public key bytes, or NULL */
    size_t   pubkey_len;
    uint8_t *der;         /* CERT / DER_PKEY DER bytes, or NULL */
    size_t   der_len;

    uint8_t *descr;       /* opaque blob (base64-decoded), or NULL */
    size_t   descr_len;
} ehem_key_details;

/*
 * Fetch one key's public material + metadata: GET /api/keymgmt/get/{kid}. `kid`
 * is validated client-side (exactly 32 hex chars, else EHEM_ERR_ARG with no
 * network I/O). The call authenticates with the EXACT per-key scope
 * "keymgmt:use:<kid>" — on firmware v1.2.2 the documented prefix scopes
 * keymgmt:get / keymgmt:gen were not accepted for this endpoint (device > doc);
 * the scope-keyed token cache turns this into one cached token per key.
 *
 * On success writes *out (caller frees with ehem_key_details_free) and returns
 * EHEM_OK. A key the device does not hold is HTTP 406 → EHEM_ERR_NOT_FOUND;
 * 401/403 map per REQ-AUTH-003; a malformed body is EHEM_ERR_PROTOCOL.
 */
EHEM_API ehem_rc ehem_key_get(ehem_ctx *ctx, const char *kid,
                              ehem_key_details **out);

/* Release a key-details struct from ehem_key_get(). NULL is a no-op. */
EHEM_API void ehem_key_details_free(ehem_key_details *details);

/* ==========================================================================
 * Key metadata update (authenticated, scope "keymgmt:upd").
 * implements: REQ-KEY-007
 * ========================================================================== */

/*
 * Rewrite a key's LABEL and (optionally) DESCR: POST /api/keymgmt/update
 * (scope "keymgmt:upd"). Does not rotate key material.
 *
 * `label` is REQUIRED — the firmware's parse rejects a body without it (400),
 * even for a descr-only intent (fw v1.2.2 api_post_keymgmt_update; the
 * label-or-descr fallback in the handler is dead code). It follows the same
 * 1..32-printable-bytes policy as create. `descr`/`descr_len` set the stored
 * DESCR (raw bytes, SDK base64-encodes, capped at 64 like create).
 *
 * WARNING — whole-record semantics (live-proven 2026-07-18, device > doc):
 * the firmware REWRITES the key's metadata record, so passing NULL / 0 for
 * descr CLEARS any stored DESCR rather than leaving it unchanged (the API
 * doc's "omitted fields left untouched" note is wrong). A caller that wants
 * to keep the existing DESCR must read it first and resend it.
 *
 * Returns EHEM_OK on the device's empty 200. EHEM_ERR_ARG (no network I/O) on
 * a NULL ctx/kid/label, a kid that is not 32 hex chars, a label outside the
 * policy, or descr_len > 64. A kid the device does not hold is HTTP 406 →
 * EHEM_ERR_NOT_FOUND; 400 → EHEM_ERR_DEVICE with payload; 401/403 map per
 * REQ-AUTH-003. NOTE for hem-tool-style consumers: renaming a key can add or
 * remove its protected-set classification (REQ-TOOL-005) — the SDK applies no
 * such policy itself.
 */
EHEM_API ehem_rc ehem_key_update(ehem_ctx *ctx, const char *kid,
                                 const char *label,
                                 const uint8_t *descr, size_t descr_len);

/* ==========================================================================
 * Public-key import (authenticated, scope "keymgmt:imp").
 * implements: REQ-KEY-008
 * ========================================================================== */

/*
 * Parameters for ehem_key_import(). `type`, `label`, and `pubkey` are
 * required; `mode` and `descr` are optional (NULL / 0 to omit). As with
 * create, the SDK keeps no type allowlist: `type` and `mode` pass through
 * verbatim and the device is the authority.
 */
typedef struct ehem_key_import_params {
    /* Algorithm literal of the imported public key, per encedo-hem-api-doc
     * keymgmt/import.md — asymmetric only ("SECP256R1", "CURVE25519",
     * "ED25519", "MLKEM768", ...). Symmetric types (AES/HMAC) are rejected by
     * the device: importing raw symmetric material is unsupported by design. */
    const char    *type;

    /* Human label, same 1..32 printable-bytes policy as create. */
    const char    *label;

    /* The RAW public key bytes (SDK base64-encodes for the wire). Encoding is
     * algorithm-native: X25519/Ed25519 raw 32 bytes, NIST curves the SEC1
     * point form the device exports from ehem_key_get() (compressed x963).
     * The firmware's nominal 70-byte decoded cap is dead code (its length
     * check is broken), so the SDK imposes no client-side length cap — the
     * device's repo import is the authority on what fits. */
    const uint8_t *pubkey;
    size_t         pubkey_len;

    /* Optional role mode, NIST-P ECC only: exact literals "ECDH", "ExDSA", or
     * "ECDH,ExDSA" (anything else is a device 400). NULL omits the field. */
    const char    *mode;

    /* Optional opaque blob, as create (raw bytes, ≤ 64, SDK base64-encodes). */
    const uint8_t *descr;
    size_t         descr_len;
} ehem_key_import_params;

/*
 * Import an external PUBLIC key into the device repository:
 * POST /api/keymgmt/import (scope "keymgmt:imp"). The stored key can then be
 * referenced by kid as the peer of crypto operations (ehem_ecdh ext_kid,
 * ehem_verify, cipher wrap). There is no private-key import.
 *
 * On success writes the new key id (32 hex chars + NUL) into `kid_out` — a
 * caller-provided buffer of at least EHEM_KID_HEX_SIZE bytes — and returns
 * EHEM_OK. EHEM_ERR_ARG (no network I/O) on NULL/missing required fields, a
 * label outside policy, an empty pubkey, or descr_len > 64. HTTP 406 means
 * the repo rejected the import — the known live cause is key deduplication
 * (this exact public key already exists on the device; python-client finding)
 * — mapped to EHEM_ERR_DEVICE with the payload retrievable; 400 →
 * EHEM_ERR_DEVICE; 401/403 map per REQ-AUTH-003.
 */
EHEM_API ehem_rc ehem_key_import(ehem_ctx *ctx,
                                 const ehem_key_import_params *params,
                                 char *kid_out);

/* ==========================================================================
 * Key-type classification (pure, client-side — no I/O, no allocation).
 * implements: REQ-KEY-006
 *
 * The device reports a key's `type` in two shapes: a bare algorithm name
 * ("ED25519" — the get/doc form) or a comma-separated flag set where role and
 * mode tokens accompany the algorithm ("ATT,PKEY,ECDH,ExDSA,SECP256R1" — the
 * list/search form on real devices). ehem_key_type_parse() decomposes either
 * shape into typed metadata so a consumer can build its local length tables
 * (PKCS#11 length queries) and attribute mappings without re-deriving the
 * device's string vocabulary. Parsing is tolerant: unrecognized tokens are
 * skipped (firmware may add flags), and a string with no recognizable
 * algorithm token classifies as EHEM_KEY_FAMILY_UNKNOWN — never an error.
 * ========================================================================== */

/* Algorithm family, named after the device's own type vocabulary. */
typedef enum ehem_key_family {
    EHEM_KEY_FAMILY_UNKNOWN = 0,
    /* NIST ECC (signing requires the ExDSA mode flag on the key) */
    EHEM_KEY_FAMILY_SECP256R1,
    EHEM_KEY_FAMILY_SECP384R1,
    EHEM_KEY_FAMILY_SECP521R1,
    EHEM_KEY_FAMILY_SECP256K1,
    /* Edwards / Montgomery */
    EHEM_KEY_FAMILY_ED25519,
    EHEM_KEY_FAMILY_ED448,
    EHEM_KEY_FAMILY_CURVE25519,     /* X25519, ECDH only */
    EHEM_KEY_FAMILY_CURVE448,       /* X448, ECDH only */
    /* Symmetric (no public material on the wire) */
    EHEM_KEY_FAMILY_AES128,
    EHEM_KEY_FAMILY_AES192,
    EHEM_KEY_FAMILY_AES256,
    EHEM_KEY_FAMILY_HMAC_SHA2_256,  /* device token "SHA2-256" */
    EHEM_KEY_FAMILY_HMAC_SHA2_384,
    EHEM_KEY_FAMILY_HMAC_SHA2_512,
    EHEM_KEY_FAMILY_HMAC_SHA3_256,
    EHEM_KEY_FAMILY_HMAC_SHA3_384,
    EHEM_KEY_FAMILY_HMAC_SHA3_512,
    /* Post-quantum */
    EHEM_KEY_FAMILY_MLKEM512,
    EHEM_KEY_FAMILY_MLKEM768,
    EHEM_KEY_FAMILY_MLKEM1024,
    EHEM_KEY_FAMILY_MLDSA44,
    EHEM_KEY_FAMILY_MLDSA65,
    EHEM_KEY_FAMILY_MLDSA87,
    /* Generic DER blobs (material arrives in ehem_key_details.der) */
    EHEM_KEY_FAMILY_CERT,           /* stored certificate */
    EHEM_KEY_FAMILY_DER_PKEY        /* DER-wrapped private key */
} ehem_key_family;

/* Mode tokens (device role bits, authoritative for NIST-P ECC — a NIST key
 * signs only when created with a mode containing ExDSA). For families whose
 * capability is intrinsic (ED*, MLDSA*), the device may omit these tokens;
 * use sig_max_len to test signability instead. */
#define EHEM_KEY_MODE_EXDSA (1u << 0)
#define EHEM_KEY_MODE_ECDH  (1u << 1)

/* Role tokens observed in the flag-set form. */
#define EHEM_KEY_ROLE_ATT         (1u << 0)  /* attestation key */
#define EHEM_KEY_ROLE_PKEY        (1u << 1)  /* private-key material present */
#define EHEM_KEY_ROLE_CERT        (1u << 2)  /* certificate */
#define EHEM_KEY_ROLE_GENERIC_DER (1u << 3)  /* generic DER container */

/*
 * Typed view of one device type string. Sizes are wire-format facts:
 *   - pubkey_len  : exact length of ehem_key_details.pubkey for this family
 *                   (0 when the family exports none — symmetric — or when the
 *                   material is variable-length DER — CERT / DER_PKEY);
 *   - sig_max_len : maximum byte length of a signature produced with this
 *                   family's native sign operation (0: the family cannot sign);
 *   - sig_der     : nonzero when the signature is DER-encoded and therefore
 *                   variable-length up to sig_max_len (NIST ECDSA); zero means
 *                   fixed-size raw bytes (Ed25519 64, Ed448 114, ML-DSA).
 * Fixed-size r‖s conversions (PKCS#11 CKM_ECDSA output) are the consumer's.
 */
typedef struct ehem_key_type_info {
    ehem_key_family family;
    unsigned        modes;        /* EHEM_KEY_MODE_* bits, tokens as sent */
    unsigned        roles;        /* EHEM_KEY_ROLE_* bits, tokens as sent */
    size_t          pubkey_len;
    size_t          sig_max_len;
    int             sig_der;
} ehem_key_type_info;

/*
 * Classify a device `type` string (either shape) into *out. Pure function:
 * no network, no allocation, safe to call per list entry. Returns EHEM_ERR_ARG
 * only on a NULL argument; every parseable input — including an empty string
 * or one with no recognized token — is EHEM_OK with family
 * EHEM_KEY_FAMILY_UNKNOWN and whatever flags were recognized.
 */
EHEM_API ehem_rc ehem_key_type_parse(const char *type, ehem_key_type_info *out);

/*
 * Stable, static display name for a family — the device's own vocabulary
 * ("SECP256R1", "ED25519", "CERT", …), "unknown" for EHEM_KEY_FAMILY_UNKNOWN
 * or an out-of-range value. Never NULL; do not free.
 */
EHEM_API const char *ehem_key_family_str(ehem_key_family family);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_KEYMGMT_H */
