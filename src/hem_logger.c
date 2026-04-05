#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hem/hem.h"
#include "hem/hem_logger.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * GET /api/logger/key  (scope: logger:get)
 * ---------------------------------------------------------------------- */
hem_error_t hem_logger_key(hem_ctx_t *ctx, hem_logger_key_t *out)
{
    if (!ctx || !out) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "logger:get");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/logger/key", ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    hem_json_get_str(root, "key",          out->key_b64,          sizeof(out->key_b64));
    hem_json_get_str(root, "nonce",        out->nonce_b64,        sizeof(out->nonce_b64));
    hem_json_get_str(root, "nonce_signed", out->nonce_signed_b64, sizeof(out->nonce_signed_b64));
    cJSON_Delete(root);

    if (!out->key_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing key in logger/key response");
        return HEM_ERR_JSON;
    }

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/logger/list/{offset}  (scope: logger:get)
 * ---------------------------------------------------------------------- */
hem_error_t hem_logger_list(hem_ctx_t *ctx, int offset,
                             int *ids_out, int ids_cap, int *count)
{
    if (!ctx || !ids_out || ids_cap <= 0 || !count) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "logger:get");
    if (err != HEM_OK) return err;

    char path[64];
    snprintf(path, sizeof(path), "/api/logger/list/%d", offset);

    err = hem_http_get(ctx, path, ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    *count = 0;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsArray(arr)) {
        cJSON *item;
        cJSON_ArrayForEach(item, arr) {
            if (*count >= ids_cap) break;
            if (cJSON_IsNumber(item))
                ids_out[(*count)++] = (int)item->valuedouble;
        }
    }

    cJSON_Delete(root);
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/logger/{id}  (scope: logger:get)
 *
 * Response is plain text, not JSON -- copy resp_buf directly.
 * ---------------------------------------------------------------------- */
hem_error_t hem_logger_download(hem_ctx_t *ctx, int log_id,
                                 char *buf, size_t buf_size, size_t *out_len)
{
    if (!ctx || !buf || buf_size == 0 || !out_len) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "logger:get");
    if (err != HEM_OK) return err;

    char path[64];
    snprintf(path, sizeof(path), "/api/logger/%d", log_id);

    err = hem_http_get(ctx, path, ctx->token);
    if (err != HEM_OK) return err;

    if (ctx->resp_len == 0) {
        buf[0]   = '\0';
        *out_len = 0;
        return HEM_OK;
    }

    if (ctx->resp_len >= buf_size) {
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "log download buffer too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(buf, ctx->resp_buf, ctx->resp_len);
    buf[ctx->resp_len] = '\0';
    *out_len = ctx->resp_len;

    return HEM_OK;
}
