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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_KEYMGMT_H */
