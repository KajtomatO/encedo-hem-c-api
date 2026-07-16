/*
 * proto_keymgmt.c — bindings for the `keymgmt` API group: the paginated
 * inventory (list + full-repo walk), descr search, create, delete, and
 * single-key get. Search returns the identical `{offset,total,listed,list}`
 * envelope as list and shares parse_key_page().
 *
 * implements: REQ-KEY-001, REQ-KEY-002, REQ-KEY-003, REQ-KEY-004, REQ-KEY-005,
 *             REQ-API-005
 *
 * Follows the proto_system.c template: build the request → send it through the
 * shared request path (proto_common — scope-based bearer, REQ-NET-005 recovery,
 * HTTP→rc mapping) → parse the JSON body into a caller-owned struct with
 * tolerant parsing (unknown fields ignored; a missing required entry field →
 * EHEM_ERR_PROTOCOL).
 */
#include "ehem/keymgmt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "ejwt.h"            /* ehem_b64_std_decode/encode — descr blobs */
#include "json.h"
#include "proto_common.h"
#include "transport.h"

#define KEYMGMT_LIST_SCOPE   "keymgmt:list"
#define KEYMGMT_GEN_SCOPE    "keymgmt:gen"
#define KEYMGMT_DEL_SCOPE    "keymgmt:del"
#define KEYMGMT_SEARCH_SCOPE "keymgmt:search"

/* A key id is exactly 32 hex chars (16 bytes); EHEM_KID_HEX_SIZE == 33 with NUL. */
#define KID_HEX_LEN 32

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

/*
 * Read an optional std-base64 string field and store its decoded bytes in *out
 * (length in *out_len). Absent, empty, or undecodable → left NULL/0 (tolerant —
 * an opaque blob the SDK does not interpret must not sink a whole parse).
 * Returns false only on allocation failure.
 */
static bool decode_opt_b64(const ehem_json *obj, const char *key,
                           uint8_t **out, size_t *out_len)
{
    const char *s;
    size_t b64_len;
    size_t n;
    uint8_t *raw;

    if (!ehem_json_get_string(obj, key, &s) || s[0] == '\0') {
        return true;                  /* absent / empty */
    }
    b64_len = strlen(s);
    raw = malloc(b64_len);            /* decoded is never larger than input */
    if (raw == NULL) {
        return false;
    }
    n = ehem_b64_std_decode(s, b64_len, raw, b64_len);
    if (n == (size_t)-1) {
        free(raw);                    /* undecodable — tolerated as absent */
        return true;
    }
    *out = raw;
    *out_len = n;
    return true;
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

    if (!decode_opt_b64(obj, "descr", &e->descr, &e->descr_len)) {
        goto oom;
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
                /* %u (not %zu): ehem_ctx_fail carries the plain printf format
                 * attribute, which is the ms_printf archetype on MinGW and
                 * rejects the C99 'z' modifier. The entry index is small. */
                return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                                     "keymgmt/list: entry %u missing required "
                                     "field '%s'", (unsigned)i, missing);
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

/* -------------------------------------------------------------------------- */
/* create + delete                                                            */
/* -------------------------------------------------------------------------- */

/* Kid validation lives in proto_common (ehem_proto_is_kid_hex) — shared with
 * the crypto bindings, which authenticate per kid too. */

/*
 * Remap a device 406 ("not in repo") to EHEM_ERR_NOT_FOUND with a fresh message;
 * any other rc passes through unchanged. The device payload is stack-copied
 * first because ehem_ctx_fail frees the current err_payload before copying, so
 * re-passing the live pointer would be a use-after-free. `what` names the op.
 */
static ehem_rc map_406_not_found(ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    char payload[256];
    const char *p;

    if (rc != EHEM_ERR_DEVICE || ehem_last_error(ctx)->http_status != 406) {
        return rc;
    }
    p = ehem_last_error(ctx)->device_payload;
    payload[0] = '\0';
    if (p != NULL) {
        snprintf(payload, sizeof payload, "%s", p);
    }
    return ehem_ctx_fail(ctx, EHEM_ERR_NOT_FOUND, 406,
                         payload[0] != '\0' ? payload : NULL,
                         "%s: key not found", what);
}

/* Label policy (REQ-KEY-005): 1..31 printable-ASCII bytes. */
static bool label_ok(const char *label)
{
    size_t n = strlen(label);
    size_t i;
    if (n < 1 || n > 31) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (!isprint((unsigned char)label[i])) {
            return false;
        }
    }
    return true;
}

ehem_rc ehem_key_create(ehem_ctx *ctx, const ehem_key_create_params *params,
                        char *kid_out)
{
    ehem_json *body_obj;
    ehem_json *root = NULL;
    char *body;
    const char *kid;
    ehem_rc rc;

    if (ctx == NULL || params == NULL || kid_out == NULL ||
        params->type == NULL || params->label == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);
    if (!label_ok(params->label)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "keymgmt/create: label must be 1..31 printable bytes");
    }

    /* Body {type,label[,mode][,descr(b64)]} in that exact order (the JSON layer
     * preserves insertion order — REQ-BUILD-003). */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_add_string(body_obj, "type", params->type) ||
        !ehem_json_add_string(body_obj, "label", params->label)) {
        goto oom_obj;
    }
    if (params->mode != NULL &&
        !ehem_json_add_string(body_obj, "mode", params->mode)) {
        goto oom_obj;
    }
    if (params->descr != NULL && params->descr_len > 0) {
        size_t enc = ehem_b64_std_encoded_len(params->descr_len);
        char *descr_b64 = malloc(enc + 1);
        size_t w;
        if (descr_b64 == NULL) {
            goto oom_obj;
        }
        w = ehem_b64_std_encode(params->descr, params->descr_len,
                                descr_b64, enc + 1);
        if (w == (size_t)-1 ||
            !ehem_json_add_string(body_obj, "descr", descr_b64)) {
            free(descr_b64);
            goto oom_obj;
        }
        free(descr_b64);
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/keymgmt/create",
                                 body, KEYMGMT_GEN_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;   /* 400 validation / 406 repo full → mapped with payload */
    }

    if (!ehem_json_get_string(root, "kid", &kid)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                           "keymgmt/create: response missing 'kid'");
    } else if (!ehem_proto_is_kid_hex(kid)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                           "keymgmt/create: 'kid' is not 32 hex chars");
    } else {
        memcpy(kid_out, kid, KID_HEX_LEN);
        kid_out[KID_HEX_LEN] = '\0';
        rc = EHEM_OK;
    }
    ehem_json_free(root);
    return rc;

oom_obj:
    ehem_json_free(body_obj);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_key_delete(ehem_ctx *ctx, const char *kid)
{
    char path[64];
    char *body = NULL;
    long status;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);
    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "keymgmt/delete: kid must be exactly 32 hex chars");
    }

    snprintf(path, sizeof path, "/api/keymgmt/delete/%s", kid);
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_DELETE, path, NULL,
                                KEYMGMT_DEL_SCOPE, EHEM_TLS_REQ_DEFAULT, &body);
    if (rc == EHEM_OK) {
        free(body);                 /* doc: empty 200; tolerate a body anyway */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }

    status = ehem_last_error(ctx)->http_status;

    /* The device answers a successful delete with an EMPTY 200; the shared path
     * reports an empty 2xx body as EHEM_ERR_PROTOCOL (http_status 200) — which
     * for a delete IS success, exactly like the reboot binding. */
    if (rc == EHEM_ERR_PROTOCOL && status == 200) {
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }

    /* 406 = kid not in the repository → NOT_FOUND (REQ-KEY-004); any other rc
     * (401/403/transport/…) passes through, already recorded. */
    return map_406_not_found(ctx, rc, "keymgmt/delete");
}

/* -------------------------------------------------------------------------- */
/* search                                                                     */
/* -------------------------------------------------------------------------- */

/* Build the wire `descr` for a search: base64(pattern) with the mode's anchor
 * ('^' prefix / '$' suffix / none). *out is heap, caller frees. */
static ehem_rc build_search_descr(ehem_key_search_mode mode,
                                  const uint8_t *pattern, size_t pattern_len,
                                  char **out)
{
    size_t enc = ehem_b64_std_encoded_len(pattern_len);
    char *descr = malloc(enc + 2);   /* +1 anchor, +1 NUL */
    size_t w;

    if (descr == NULL) {
        return EHEM_ERR_NOMEM;
    }
    if (mode == EHEM_KEY_SEARCH_PREFIX) {
        descr[0] = '^';
        w = ehem_b64_std_encode(pattern, pattern_len, descr + 1, enc + 1);
    } else {
        w = ehem_b64_std_encode(pattern, pattern_len, descr, enc + 1);
    }
    if (w == (size_t)-1) {
        free(descr);
        return EHEM_ERR_NOMEM;        /* only fails on bad args/capacity */
    }
    if (mode == EHEM_KEY_SEARCH_SUFFIX) {
        descr[w] = '$';
        descr[w + 1] = '\0';
    }
    *out = descr;
    return EHEM_OK;
}

/* One search POST → a page. A device 404 ("no keys matched") becomes EHEM_OK
 * with an empty page (REQ-KEY-002: precedence device > doc). */
static ehem_rc search_one_page(ehem_ctx *ctx, const char *descr,
                               size_t offset, size_t limit,
                               ehem_key_page **out)
{
    ehem_json *body_obj;
    ehem_json *root = NULL;
    char *body;
    ehem_rc rc;

    /* Body {descr, offset, limit} in that order. */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_add_string(body_obj, "descr", descr) ||
        !ehem_json_add_int64(body_obj, "offset", (int64_t)offset) ||
        !ehem_json_add_int64(body_obj, "limit", (int64_t)limit)) {
        ehem_json_free(body_obj);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/keymgmt/search",
                                 body, KEYMGMT_SEARCH_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    ehem_json_string_free(body);

    if (rc == EHEM_ERR_NOT_FOUND) {
        /* 404 = nothing matched → empty page, clean success. */
        ehem_key_page *page = calloc(1, sizeof *page);
        if (page == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
        ehem_ctx_clear_error(ctx);
        *out = page;
        return EHEM_OK;
    }
    if (rc != EHEM_OK) {
        return rc;   /* 400/406/410 → mapped with payload; 401/403 per AUTH-003 */
    }

    rc = parse_key_page(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

/* Validate the shared search arguments (no I/O). */
static bool search_args_ok(const ehem_ctx *ctx, const uint8_t *pattern,
                           size_t pattern_len, ehem_key_search_mode mode,
                           const void *out)
{
    if (ctx == NULL || out == NULL) {
        return false;
    }
    if (pattern == NULL && pattern_len > 0) {
        return false;
    }
    return (mode == EHEM_KEY_SEARCH_SUBSTRING ||
            mode == EHEM_KEY_SEARCH_PREFIX ||
            mode == EHEM_KEY_SEARCH_SUFFIX);
}

ehem_rc ehem_key_search(ehem_ctx *ctx, const uint8_t *pattern, size_t pattern_len,
                        ehem_key_search_mode mode, size_t offset, size_t limit,
                        ehem_key_page **out)
{
    char *descr;
    ehem_rc rc;

    if (!search_args_ok(ctx, pattern, pattern_len, mode, out)) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = build_search_descr(mode, pattern, pattern_len, &descr);
    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, rc, 0, NULL, "out of memory");
    }
    rc = search_one_page(ctx, descr, offset, limit, out);
    free(descr);
    return rc;
}

ehem_rc ehem_key_search_all(ehem_ctx *ctx, const uint8_t *pattern,
                            size_t pattern_len, ehem_key_search_mode mode,
                            ehem_key_page **out)
{
    char *descr;
    ehem_key_page *acc;
    size_t offset = 0;
    ehem_rc rc;

    if (!search_args_ok(ctx, pattern, pattern_len, mode, out)) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = build_search_descr(mode, pattern, pattern_len, &descr);
    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, rc, 0, NULL, "out of memory");
    }

    acc = calloc(1, sizeof *acc);
    if (acc == NULL) {
        free(descr);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    for (;;) {
        ehem_key_page *page = NULL;
        size_t listed;

        rc = search_one_page(ctx, descr, offset, KEYMGMT_WALK_PAGE, &page);
        if (rc != EHEM_OK) {
            free(descr);
            ehem_key_page_free(acc);
            return rc;
        }
        acc->total = page->total;
        listed = page->listed;
        if (!page_append(acc, page)) {
            ehem_key_page_free(page);
            free(descr);
            ehem_key_page_free(acc);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
        ehem_key_page_free(page);

        offset += listed;
        if (listed == 0 || (int64_t)offset >= acc->total) {
            break;
        }
    }

    free(descr);
    *out = acc;
    return EHEM_OK;
}

/* -------------------------------------------------------------------------- */
/* get (single key by kid)                                                    */
/* -------------------------------------------------------------------------- */

/*
 * Parse a get response into caller-owned details. `type` is required; `updated`
 * defaults to 0; at most one of pubkey/der is present (the device sets only the
 * one that applies); `descr` optional. `label` is never sent by this endpoint.
 */
static ehem_rc parse_key_details(ehem_ctx *ctx, const ehem_json *root,
                                 ehem_key_details **out)
{
    ehem_key_details *d = calloc(1, sizeof *d);
    const char *type;

    if (d == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_get_string(root, "type", &type)) {
        ehem_key_details_free(d);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "keymgmt/get: missing required field 'type'");
    }
    d->type = dup_str(type);
    if (d->type == NULL) {
        goto oom;
    }
    ehem_json_get_int64(root, "updated", &d->updated);
    if (!decode_opt_b64(root, "pubkey", &d->pubkey, &d->pubkey_len) ||
        !decode_opt_b64(root, "der", &d->der, &d->der_len) ||
        !decode_opt_b64(root, "descr", &d->descr, &d->descr_len)) {
        goto oom;
    }

    *out = d;
    return EHEM_OK;

oom:
    ehem_key_details_free(d);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_key_get(ehem_ctx *ctx, const char *kid, ehem_key_details **out)
{
    char path[64];
    char scope[64];
    ehem_json *root = NULL;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);
    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "keymgmt/get: kid must be exactly 32 hex chars");
    }

    /* Exact per-key scope keymgmt:use:<kid> (device > doc — REQ-KEY-003). */
    snprintf(path, sizeof path, "/api/keymgmt/get/%s", kid);
    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, path, NULL, scope,
                                 EHEM_TLS_REQ_DEFAULT, &root);
    if (rc != EHEM_OK) {
        return map_406_not_found(ctx, rc, "keymgmt/get");
    }
    rc = parse_key_details(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

void ehem_key_details_free(ehem_key_details *details)
{
    if (details == NULL) {
        return;
    }
    free(details->type);
    free(details->pubkey);
    free(details->der);
    free(details->descr);
    free(details);
}
