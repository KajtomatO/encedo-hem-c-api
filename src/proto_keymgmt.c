/*
 * proto_keymgmt.c — bindings for the `keymgmt` API group. This first cut covers
 * the paginated inventory (REQ-KEY-001): the single-page list and the full-repo
 * walk. Search (REQ-KEY-002) returns the identical `{offset,total,listed,list}`
 * envelope and will reuse parse_key_page() here.
 *
 * implements: REQ-KEY-001, REQ-API-005
 *
 * Follows the proto_system.c template: build the request → send it through the
 * shared request path (proto_common — scope-based bearer, REQ-NET-005 recovery,
 * HTTP→rc mapping) → parse the JSON body into a caller-owned struct with
 * tolerant parsing (unknown fields ignored; a missing required entry field →
 * EHEM_ERR_PROTOCOL).
 */
#include "ehem/keymgmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "ejwt.h"            /* ehem_b64_std_decode — descr blobs */
#include "json.h"
#include "proto_common.h"
#include "transport.h"

#define KEYMGMT_LIST_SCOPE "keymgmt:list"

/* The device caps a page at 15 server-side; walk in sub-cap pages (python OQ-17
 * uses 10 — "stay safely below"). */
#define KEYMGMT_WALK_PAGE 10

/* -------------------------------------------------------------------------- */
/* helpers                                                                    */
/* -------------------------------------------------------------------------- */

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* Copy an optional string field into *dst if present. Returns false only on
 * allocation failure; an absent field is success (leaves *dst NULL). */
static bool opt_str(const ehem_json *obj, const char *key, char **dst)
{
    const char *s;
    if (!ehem_json_get_string(obj, key, &s)) {
        return true;   /* absent — fine */
    }
    *dst = dup_str(s);
    return (*dst != NULL);
}

/* Release the contents of an entry (not the entry pointer itself). */
static void entry_dispose(ehem_key_entry *e)
{
    free(e->kid);
    free(e->type);
    free(e->label);
    free(e->descr);
}

/* -------------------------------------------------------------------------- */
/* entry / page parsing                                                       */
/* -------------------------------------------------------------------------- */

/*
 * Parse one `list[]` element into *e (already zeroed). `kid` and `type` are
 * required — a missing one yields EHEM_ERR_PROTOCOL (via *missing). `descr`, when
 * present and non-empty, is base64-decoded into e->descr/descr_len; an
 * undecodable descr is tolerated as absent (the field is opaque to the SDK and
 * a single odd blob must not sink a whole listing).
 */
static ehem_rc parse_entry(const ehem_json *obj, ehem_key_entry *e,
                           const char **missing)
{
    const char *str;

    if (!ehem_json_get_string(obj, "kid", &str)) {
        *missing = "kid";
        return EHEM_ERR_PROTOCOL;
    }
    if ((e->kid = dup_str(str)) == NULL) {
        goto oom;
    }
    if (!ehem_json_get_string(obj, "type", &str)) {
        *missing = "type";
        return EHEM_ERR_PROTOCOL;
    }
    if ((e->type = dup_str(str)) == NULL) {
        goto oom;
    }

    if (!opt_str(obj, "label", &e->label)) {
        goto oom;
    }

    if (ehem_json_get_string(obj, "descr", &str) && str[0] != '\0') {
        size_t b64_len = strlen(str);
        uint8_t *raw = malloc(b64_len);   /* decoded is never larger than input */
        size_t n;
        if (raw == NULL) {
            goto oom;
        }
        n = ehem_b64_std_decode(str, b64_len, raw, b64_len);
        if (n == (size_t)-1) {
            free(raw);                    /* undecodable — tolerated as absent */
        } else {
            e->descr = raw;
            e->descr_len = n;
        }
    }

    /* Optional timestamps (default 0 — the device always sends them in practice). */
    ehem_json_get_int64(obj, "created", &e->created);
    ehem_json_get_int64(obj, "updated", &e->updated);

    return EHEM_OK;

oom:
    return EHEM_ERR_NOMEM;
}

/*
 * Parse a `{offset,total,listed,list:[…]}` envelope into a caller-owned page.
 * Shared by list and (later) search. page->listed and page->entries reflect the
 * actual parsed `list` array; offset/total echo the device (tolerant defaults).
 */
static ehem_rc parse_key_page(ehem_ctx *ctx, const ehem_json *root,
                              ehem_key_page **out)
{
    ehem_key_page *page = calloc(1, sizeof *page);
    const ehem_json *list;
    size_t n;
    size_t i;

    if (page == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Page-level scalars are tolerant: offset echoes back, total drives the
     * walk. Absent → 0 (a malformed device degrades to a one-page result). */
    ehem_json_get_int64(root, "offset", &page->offset);
    ehem_json_get_int64(root, "total", &page->total);

    list = ehem_json_get(root, "list");
    n = ehem_json_is_array(list) ? ehem_json_array_size(list) : 0;
    if (n == 0) {
        *out = page;                  /* empty page: listed 0, entries NULL */
        return EHEM_OK;
    }

    page->entries = calloc(n, sizeof *page->entries);
    if (page->entries == NULL) {
        free(page);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    for (i = 0; i < n; i++) {
        const char *missing = NULL;
        ehem_rc rc = parse_entry(ehem_json_array_get(list, i),
                                 &page->entries[i], &missing);
        if (rc != EHEM_OK) {
            page->listed = i + 1;     /* entry i is partially built; free it too */
            ehem_key_page_free(page);
            if (rc == EHEM_ERR_PROTOCOL) {
                return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                                     "keymgmt/list: entry %zu missing required "
                                     "field '%s'", i, missing);
            }
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
    }
    page->listed = n;

    *out = page;
    return EHEM_OK;
}

void ehem_key_page_free(ehem_key_page *page)
{
    size_t i;
    if (page == NULL) {
        return;
    }
    for (i = 0; i < page->listed; i++) {
        entry_dispose(&page->entries[i]);
    }
    free(page->entries);
    free(page);
}

/* -------------------------------------------------------------------------- */
/* list (single page)                                                         */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_key_list(ehem_ctx *ctx, size_t offset, size_t limit,
                      ehem_key_page **out)
{
    char path[64];
    ehem_json *root = NULL;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    /* GET /api/keymgmt/list[/{offset}[/{limit}]] — only append the segments the
     * caller actually constrains (limit 0 → device default; offset 0 & no limit
     * → the bare path). */
    if (limit > 0) {
        snprintf(path, sizeof path, "/api/keymgmt/list/%zu/%zu", offset, limit);
    } else if (offset > 0) {
        snprintf(path, sizeof path, "/api/keymgmt/list/%zu", offset);
    } else {
        snprintf(path, sizeof path, "/api/keymgmt/list");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, path, NULL,
                                 KEYMGMT_LIST_SCOPE, EHEM_TLS_REQ_DEFAULT, &root);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = parse_key_page(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

/* -------------------------------------------------------------------------- */
/* list_all (full-repository walk)                                            */
/* -------------------------------------------------------------------------- */

/* Append `src`'s entries onto `dst`, transferring ownership of each entry's
 * heap fields (src's array is emptied so freeing src frees nothing twice).
 * Returns false on allocation failure (dst/src both still valid to free). */
static bool page_append(ehem_key_page *dst, ehem_key_page *src)
{
    size_t need;
    ehem_key_entry *grown;

    if (src->listed == 0) {
        return true;
    }
    need = dst->listed + src->listed;
    grown = realloc(dst->entries, need * sizeof *grown);
    if (grown == NULL) {
        return false;
    }
    dst->entries = grown;
    memcpy(dst->entries + dst->listed, src->entries,
           src->listed * sizeof *src->entries);
    dst->listed = need;

    /* The entries now belong to dst; detach them from src. */
    free(src->entries);
    src->entries = NULL;
    src->listed = 0;
    return true;
}

ehem_rc ehem_key_list_all(ehem_ctx *ctx, ehem_key_page **out)
{
    ehem_key_page *acc;
    size_t offset = 0;
    ehem_rc rc = EHEM_OK;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    acc = calloc(1, sizeof *acc);       /* merged result: offset 0, entries[] */
    if (acc == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    for (;;) {
        ehem_key_page *page = NULL;
        size_t listed;

        rc = ehem_key_list(ctx, offset, KEYMGMT_WALK_PAGE, &page);
        if (rc != EHEM_OK) {
            ehem_key_page_free(acc);
            return rc;                  /* ehem_key_list already recorded detail */
        }

        acc->total = page->total;       /* keep the latest reported total */
        listed = page->listed;
        if (!page_append(acc, page)) {
            ehem_key_page_free(page);
            ehem_key_page_free(acc);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
        ehem_key_page_free(page);

        /*
         * Terminate on offset >= total or an empty page (REQ-KEY-001) — NOT on
         * "listed < requested page size", which is unreliable against the
         * server-side 15-cap (python OQ-17).
         */
        offset += listed;
        if (listed == 0 || (int64_t)offset >= acc->total) {
            break;
        }
    }

    *out = acc;
    return EHEM_OK;
}
