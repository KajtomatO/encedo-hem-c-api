/*
 * proto_logger.c — bindings for the `logger` API group: the audit-log
 * signing key (key), the log-file listing (list), and raw log-file download
 * (get). Listing and download are PPA-only routes (microSD); on EPA builds
 * they 404 → EHEM_ERR_NOT_FOUND, which callers treat as "not available on
 * this device".
 *
 * implements: REQ-SYS-009, REQ-API-005
 *
 * Follows the proto_system.c template: build the request → shared request
 * path (scope-based bearer, recovery, HTTP→rc mapping) → tolerant parse into
 * caller-owned structs. The download deliberately uses the RAW request path:
 * the body is text/plain log records, not JSON.
 */
#include "ehem/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "ejwt.h"            /* ehem_b64_std_decode — key/nonce/signature */
#include "json.h"
#include "proto_common.h"
#include "transport.h"

#define LOGGER_SCOPE "logger:get"

/* -------------------------------------------------------------------------- */
/* key                                                                        */
/* -------------------------------------------------------------------------- */

/* Decode obj[key] as std base64 into exactly `want` bytes. */
static bool decode_fixed_b64(const ehem_json *obj, const char *key,
                             uint8_t *dst, size_t want)
{
    const char *s;
    uint8_t buf[128];
    size_t n;

    if (!ehem_json_get_string(obj, key, &s)) {
        return false;
    }
    n = ehem_b64_std_decode(s, strlen(s), buf, sizeof buf);
    if (n != want) {
        return false;
    }
    memcpy(dst, buf, want);
    return true;
}

ehem_rc ehem_logger_key(ehem_ctx *ctx, ehem_logger_key_info **out)
{
    ehem_json *root = NULL;
    ehem_logger_key_info *info;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, "/api/logger/key",
                                 NULL, LOGGER_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    if (rc != EHEM_OK) {
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    /* Strict sizes: a signing key that is not 32/32/64 bytes is unusable. */
    if (!decode_fixed_b64(root, "key", info->key, EHEM_LOGGER_KEY_SIZE) ||
        !decode_fixed_b64(root, "nonce", info->nonce, EHEM_LOGGER_NONCE_SIZE) ||
        !decode_fixed_b64(root, "nonce_signed", info->nonce_signed,
                          EHEM_LOGGER_SIG_SIZE)) {
        free(info);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "logger/key: key/nonce/nonce_signed must base64-"
                             "decode to 32/32/64 bytes");
    }
    ehem_json_free(root);
    *out = info;
    return EHEM_OK;
}

void ehem_logger_key_free(ehem_logger_key_info *info)
{
    free(info);
}

/* -------------------------------------------------------------------------- */
/* list                                                                       */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_logger_list(ehem_ctx *ctx, size_t offset, ehem_logger_page **out)
{
    char path[64];
    ehem_json *root = NULL;
    const ehem_json *ids;
    ehem_logger_page *page;
    size_t n;
    size_t i;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    snprintf(path, sizeof path, "/api/logger/list/%lu", (unsigned long)offset);
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, path, NULL, LOGGER_SCOPE,
                                 EHEM_TLS_REQ_DEFAULT, &root);
    if (rc != EHEM_OK) {
        return rc;   /* EPA/listing-failure 404 → NOT_FOUND; 406 → DEVICE */
    }

    page = calloc(1, sizeof *page);
    if (page == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    ehem_json_get_int64(root, "total", &page->total);

    ids = ehem_json_get(root, "id");
    n = ehem_json_is_array(ids) ? ehem_json_array_size(ids) : 0;
    if (n > 0) {
        page->ids = calloc(n, sizeof *page->ids);
        if (page->ids == NULL) {
            goto oom;
        }
        for (i = 0; i < n; i++) {
            const char *s = NULL;
            if (!ehem_json_as_string(ehem_json_array_get(ids, i), &s)) {
                page->count = i;
                ehem_logger_page_free(page);
                ehem_json_free(root);
                return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                                     "logger/list: 'id' element %u is not a "
                                     "string", (unsigned)i);
            }
            page->ids[i] = malloc(strlen(s) + 1);
            if (page->ids[i] == NULL) {
                page->count = i;
                goto oom;
            }
            memcpy(page->ids[i], s, strlen(s) + 1);
        }
        page->count = n;
    }
    ehem_json_free(root);
    *out = page;
    return EHEM_OK;

oom:
    ehem_logger_page_free(page);
    ehem_json_free(root);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

void ehem_logger_page_free(ehem_logger_page *page)
{
    size_t i;
    if (page == NULL) {
        return;
    }
    for (i = 0; i < page->count; i++) {
        free(page->ids[i]);
    }
    free(page->ids);
    free(page);
}

/* -------------------------------------------------------------------------- */
/* get (raw download)                                                         */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_logger_get(ehem_ctx *ctx, const char *id,
                        uint8_t **data, size_t *len)
{
    char path[96];
    char *body = NULL;
    size_t i;
    ehem_rc rc;

    if (ctx == NULL || id == NULL || id[0] == '\0' || data == NULL ||
        len == NULL) {
        return EHEM_ERR_ARG;
    }
    *data = NULL;
    *len = 0;
    ehem_ctx_clear_error(ctx);

    /* The id is a hex filename from logger/list; reject anything that could
     * change the path shape (defense against "list" aliasing and separators). */
    for (i = 0; id[i] != '\0'; i++) {
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f') ||
              (id[i] >= 'A' && id[i] <= 'F'))) {
            return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                                 "logger/get: id must be hex");
        }
    }
    if (i >= 32) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "logger/get: id too long");
    }

    snprintf(path, sizeof path, "/api/logger/%s", id);
    /* RAW path: the body is text/plain log records, not JSON. The transport
     * NUL-terminates the buffer; record length via the last-error-free size
     * probe below. */
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_GET, path, NULL, LOGGER_SCOPE,
                                EHEM_TLS_REQ_DEFAULT, &body);
    if (rc != EHEM_OK) {
        return rc;   /* 404 → NOT_FOUND; 406 (FR_LOCKED) → DEVICE */
    }
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "logger/get: empty body");
    }
    *data = (uint8_t *)body;
    *len = strlen(body);
    return EHEM_OK;
}

void ehem_logger_file_free(uint8_t *data)
{
    free(data);
}
