#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hem/hem.h"
#include "hem/hem_keymgmt.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/create  (scope: keymgmt:gen)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_create(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            char       *kid_out,
                            size_t      kid_size)
{
    if (!ctx || !label || !type || !kid_out || kid_size < 33)
        return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "keymgmt:gen");
    if (err != HEM_OK) return err;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "label", label);
    cJSON_AddStringToObject(req, "type",  type);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/keymgmt/create", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    hem_json_get_str(root, "kid", kid_out, kid_size);
    cJSON_Delete(root);

    if (!kid_out[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "no kid in create response");
        return HEM_ERR_JSON;
    }
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * DELETE /api/keymgmt/delete/{kid}  (scope: keymgmt:del)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_delete(hem_ctx_t *ctx, const char *kid)
{
    if (!ctx || !kid || !kid[0]) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "keymgmt:del");
    if (err != HEM_OK) return err;

    char path[80];
    snprintf(path, sizeof(path), "/api/keymgmt/delete/%s", kid);

    return hem_http_delete(ctx, path, ctx->token);
}

/* -------------------------------------------------------------------------
 * GET /api/keymgmt/list/{offset}/{limit}  (scope: keymgmt:list)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_list(hem_ctx_t      *ctx,
                          int             offset,
                          int             limit,
                          hem_key_info_t *list,
                          int             list_cap,
                          int            *total,
                          int            *listed)
{
    if (!ctx || !list || list_cap <= 0 || !total || !listed)
        return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "keymgmt:list");
    if (err != HEM_OK) return err;

    char path[64];
    snprintf(path, sizeof(path), "/api/keymgmt/list/%d/%d", offset, limit);

    err = hem_http_get(ctx, path, ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    *total  = (int)hem_json_get_int64(root, "total",  0);
    *listed = (int)hem_json_get_int64(root, "listed", 0);

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "list");
    int count = 0;
    if (cJSON_IsArray(arr)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, arr) {
            if (count >= list_cap) break;
            hem_json_get_str(entry, "kid",   list[count].kid,   sizeof(list[count].kid));
            hem_json_get_str(entry, "label", list[count].label, sizeof(list[count].label));
            hem_json_get_str(entry, "type",  list[count].type,  sizeof(list[count].type));
            list[count].created = hem_json_get_int64(entry, "created", 0);
            list[count].updated = hem_json_get_int64(entry, "updated", 0);
            count++;
        }
    }

    cJSON_Delete(root);
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/keymgmt/get/{kid}  (scope: keymgmt:list)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_get(hem_ctx_t      *ctx,
                         const char     *kid,
                         hem_key_info_t *out)
{
    if (!ctx || !kid || !kid[0] || !out) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "keymgmt:list");
    if (err != HEM_OK) return err;

    char path[80];
    snprintf(path, sizeof(path), "/api/keymgmt/get/%s", kid);

    err = hem_http_get(ctx, path, ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    strncpy(out->kid, kid, sizeof(out->kid) - 1);
    hem_json_get_str(root, "type",   out->type,  sizeof(out->type));
    out->updated = hem_json_get_int64(root, "updated", 0);

    cJSON_Delete(root);
    return HEM_OK;
}
