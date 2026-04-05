#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>   /* sleep() */

#include "hem/hem.h"
#include "hem/hem_system.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * GET /api/system/version  (no auth)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_version(hem_ctx_t *ctx, hem_version_t *out)
{
    if (!ctx || !out) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_http_get(ctx, "/api/system/version", NULL);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    hem_json_get_str(root, "hwv", out->hwv, sizeof(out->hwv));
    hem_json_get_str(root, "blv", out->blv, sizeof(out->blv));
    hem_json_get_str(root, "fwv", out->fwv, sizeof(out->fwv));
    hem_json_get_str(root, "fws", out->fws, sizeof(out->fws));
    hem_json_get_str(root, "uis", out->uis, sizeof(out->uis));

    cJSON_Delete(root);
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/status  (no auth)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_status(hem_ctx_t *ctx, hem_status_t *out)
{
    if (!ctx || !out) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_http_get(ctx, "/api/system/status", NULL);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    out->fls_state   = (int)hem_json_get_int64(root, "fls_state", 0);
    out->ts          = hem_json_get_int64(root, "ts", -1);
    out->uptime      = (int)hem_json_get_int64(root, "uptime", 0);
    out->temp        = (int)hem_json_get_int64(root, "temp", 0);
    out->https       = hem_json_get_bool(root, "https", false);
    hem_json_get_str(root, "hostname", out->hostname, sizeof(out->hostname));

    /*
     * 'inited' key is PRESENT when device is NOT yet initialized.
     * It is absent once initialization is complete.
     */
    out->initialized = !cJSON_HasObjectItem(root, "inited");

    cJSON_Delete(root);
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * Two-phase check-in  (no auth)
 *
 * Phase 1: GET  /api/system/checkin        -> {"check": "<challenge>"}
 * Phase 2: POST https://api.encedo.com/checkin with the full phase-1 body
 *          -> {"checked": "<response>"}
 * Phase 3: POST /api/system/checkin with the full phase-2 body
 *          -> {"status": "OK"}
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_checkin(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    /* --- Phase 1: get challenge from device --- */
    hem_error_t err = hem_http_get(ctx, "/api/system/checkin", NULL);
    if (err != HEM_OK) return err;

    /* Save the challenge body -- we forward it verbatim to the backend */
    char *phase1_body = NULL;
    if (ctx->resp_len > 0) {
        phase1_body = malloc(ctx->resp_len + 1);
        if (!phase1_body) {
            hem_set_error(ctx, HEM_ERR_HTTP, "out of memory");
            return HEM_ERR_HTTP;
        }
        memcpy(phase1_body, ctx->resp_buf, ctx->resp_len + 1);
    } else {
        hem_set_error(ctx, HEM_ERR_JSON, "empty check-in challenge response");
        return HEM_ERR_JSON;
    }

    /* --- Phase 2: forward challenge to Encedo backend --- */
    err = hem_http_post_url(ctx, "https://api.encedo.com/checkin", phase1_body);
    free(phase1_body);

    if (err != HEM_OK) {
        hem_set_error(ctx, HEM_ERR_CHECKIN, "backend check-in request failed");
        return HEM_ERR_CHECKIN;
    }

    /* Save backend response */
    char *phase2_body = NULL;
    if (ctx->resp_len > 0) {
        phase2_body = malloc(ctx->resp_len + 1);
        if (!phase2_body) {
            hem_set_error(ctx, HEM_ERR_HTTP, "out of memory");
            return HEM_ERR_HTTP;
        }
        memcpy(phase2_body, ctx->resp_buf, ctx->resp_len + 1);
    } else {
        hem_set_error(ctx, HEM_ERR_CHECKIN, "empty check-in backend response");
        return HEM_ERR_CHECKIN;
    }

    /* --- Phase 3: complete check-in on device --- */
    err = hem_http_post(ctx, "/api/system/checkin", phase2_body, NULL);
    free(phase2_body);

    if (err != HEM_OK) return err;

    /* Verify response contains {"status": "OK"} */
    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char status[32] = {0};
    hem_json_get_str(root, "status", status, sizeof(status));
    cJSON_Delete(root);

    if (strcmp(status, "OK") != 0) {
        hem_set_error(ctx, HEM_ERR_CHECKIN, "check-in did not return status OK");
        return HEM_ERR_CHECKIN;
    }

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/config  (requires scope "system:config")
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_config(hem_ctx_t *ctx, hem_config_t *out)
{
    if (!ctx || !out) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/system/config", ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    hem_json_get_str(root, "eid",      out->eid,      sizeof(out->eid));
    hem_json_get_str(root, "user",     out->user,     sizeof(out->user));
    hem_json_get_str(root, "email",    out->email,    sizeof(out->email));
    hem_json_get_str(root, "hostname", out->hostname, sizeof(out->hostname));
    out->uts = hem_json_get_int64(root, "uts", 0);

    cJSON_Delete(root);
    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/system/config  (scope: system:config)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_config_set(hem_ctx_t           *ctx,
                                   const char          *user_name,
                                   const char          *tls_json,
                                   hem_config_update_t *result)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;
    if (!user_name && !tls_json) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    cJSON *req = cJSON_CreateObject();

    if (user_name)
        cJSON_AddStringToObject(req, "user", user_name);

    if (tls_json) {
        cJSON *tls = cJSON_Parse(tls_json);
        if (!tls) {
            cJSON_Delete(req);
            hem_set_error(ctx, HEM_ERR_INVALID_ARG, "tls_json is not valid JSON");
            return HEM_ERR_INVALID_ARG;
        }
        cJSON_AddItemToObject(req, "tls", tls);
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/system/config", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    if (result) {
        cJSON *root = hem_json_parse_response(ctx);
        if (!root) return HEM_ERR_JSON;
        result->updated         = hem_json_get_bool(root, "updated",         false);
        result->reboot_required = hem_json_get_bool(root, "reboot_required", false);
        cJSON_Delete(root);
    }

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/reboot  (scope: system:config)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_reboot(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/system/reboot", ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char status[32] = {0};
    hem_json_get_str(root, "status", status, sizeof(status));
    cJSON_Delete(root);

    if (strcmp(status, "OK") != 0) {
        hem_set_error(ctx, HEM_ERR_JSON, "reboot response missing status OK");
        return HEM_ERR_JSON;
    }

    /* Invalidate cached token -- device sessions are lost after reboot */
    ctx->token[0]       = '\0';
    ctx->token_scope[0] = '\0';
    ctx->token_exp      = 0;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/shutdown  (scope: system:config)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_shutdown(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/system/shutdown", ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char status[32] = {0};
    hem_json_get_str(root, "status", status, sizeof(status));
    cJSON_Delete(root);

    if (strcmp(status, "OK") != 0) {
        hem_set_error(ctx, HEM_ERR_JSON, "shutdown response missing status OK");
        return HEM_ERR_JSON;
    }

    /* Invalidate cached token -- device is now offline */
    ctx->token[0]       = '\0';
    ctx->token_scope[0] = '\0';
    ctx->token_exp      = 0;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/selftest  (scope: system:config)
 * Polls every 2 seconds until kat_busy is absent; max 120 polls (240 s).
 * ---------------------------------------------------------------------- */
#define SELFTEST_POLL_INTERVAL_S  2
#define SELFTEST_MAX_POLLS       120

hem_error_t hem_system_selftest(hem_ctx_t *ctx, int *fls_state_out)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    for (int i = 0; i < SELFTEST_MAX_POLLS; i++) {
        err = hem_http_get(ctx, "/api/system/selftest", ctx->token);
        if (err != HEM_OK) return err;

        cJSON *root = hem_json_parse_response(ctx);
        if (!root) return HEM_ERR_JSON;

        bool busy = hem_json_get_bool(root, "kat_busy", false);

        if (!busy) {
            int fls = (int)hem_json_get_int64(root, "fls_state", 0);
            cJSON_Delete(root);
            if (fls_state_out) *fls_state_out = fls;
            return HEM_OK;
        }

        cJSON_Delete(root);

        if (i + 1 < SELFTEST_MAX_POLLS)
            sleep(SELFTEST_POLL_INTERVAL_S);
    }

    hem_set_error(ctx, HEM_ERR_HTTP_STATUS, "selftest timed out after 240 seconds");
    return HEM_ERR_HTTP_STATUS;
}

/* -------------------------------------------------------------------------
 * GET /api/system/config/attestation  (any valid token)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_attestation(hem_ctx_t *ctx,
                                    char      *genuine_out,
                                    size_t     genuine_size)
{
    if (!ctx || !genuine_out || genuine_size == 0) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/system/config/attestation", ctx->token);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    hem_json_get_str(root, "genuine", genuine_out, genuine_size);
    cJSON_Delete(root);

    if (!genuine_out[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing genuine in attestation response");
        return HEM_ERR_JSON;
    }

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/system/config/provisioning  (scope: system:config)
 * ---------------------------------------------------------------------- */
hem_error_t hem_system_provision(hem_ctx_t  *ctx,
                                  const char *crt,
                                  const char *genuine)
{
    if (!ctx || !crt || !crt[0] || !genuine || !genuine[0])
        return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:config");
    if (err != HEM_OK) return err;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "crt",     crt);
    cJSON_AddStringToObject(req, "genuine", genuine);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/system/config/provisioning", body, ctx->token);
    free(body);
    return err;
}
