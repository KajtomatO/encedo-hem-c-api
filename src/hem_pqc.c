#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hem/hem.h"
#include "hem/hem_pqc.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * POST /api/crypto/pqc/mlkem/encaps  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_mlkem_encaps(hem_ctx_t *ctx,
                              const char *kid,
                              uint8_t    *ss_out,
                              size_t      ss_size,
                              size_t     *ss_len,
                              uint8_t    *ct_out,
                              size_t      ct_size,
                              size_t     *ct_len)
{
    if (!ctx || !kid || !ss_out || !ss_len || !ct_out || !ct_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    snprintf(scope, sizeof(scope), "keymgmt:use:%s", kid);

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/pqc/mlkem/encaps", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char ss_b64[256] = {0};
    char ct_b64[2176] = {0};
    hem_json_get_str(root, "ss", ss_b64, sizeof(ss_b64));
    hem_json_get_str(root, "ct", ct_b64, sizeof(ct_b64));
    cJSON_Delete(root);

    if (!ss_b64[0] || !ct_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing ss or ct in mlkem/encaps response");
        return HEM_ERR_JSON;
    }

    size_t dec_len = 0;
    uint8_t *dec = hem_base64_decode(ss_b64, &dec_len);
    if (!dec) return HEM_ERR_JSON;
    if (dec_len > ss_size) { free(dec); return HEM_ERR_BUFFER_TOO_SMALL; }
    memcpy(ss_out, dec, dec_len);
    free(dec);
    *ss_len = dec_len;

    dec = hem_base64_decode(ct_b64, &dec_len);
    if (!dec) return HEM_ERR_JSON;
    if (dec_len > ct_size) { free(dec); return HEM_ERR_BUFFER_TOO_SMALL; }
    memcpy(ct_out, dec, dec_len);
    free(dec);
    *ct_len = dec_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/pqc/mlkem/decaps  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_mlkem_decaps(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *ciphertext,
                              size_t         ct_len,
                              uint8_t       *ss_out,
                              size_t         ss_size,
                              size_t        *ss_len)
{
    if (!ctx || !kid || !ciphertext || !ss_out || !ss_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    snprintf(scope, sizeof(scope), "keymgmt:use:%s", kid);

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *ct_b64 = hem_base64_encode(ciphertext, ct_len);
    if (!ct_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "ct",  ct_b64);
    free(ct_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/pqc/mlkem/decaps", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char ss_b64[256] = {0};
    hem_json_get_str(root, "ss", ss_b64, sizeof(ss_b64));
    cJSON_Delete(root);

    if (!ss_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing ss in mlkem/decaps response");
        return HEM_ERR_JSON;
    }

    size_t dec_len = 0;
    uint8_t *dec = hem_base64_decode(ss_b64, &dec_len);
    if (!dec) return HEM_ERR_JSON;
    if (dec_len > ss_size) { free(dec); return HEM_ERR_BUFFER_TOO_SMALL; }
    memcpy(ss_out, dec, dec_len);
    free(dec);
    *ss_len = dec_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/pqc/mldsa/sign  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_mldsa_sign(hem_ctx_t     *ctx,
                            const char    *kid,
                            const uint8_t *msg,
                            size_t         msg_len,
                            uint8_t       *sig_out,
                            size_t         sig_size,
                            size_t        *sig_len)
{
    if (!ctx || !kid || !msg || !sig_out || !sig_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    snprintf(scope, sizeof(scope), "keymgmt:use:%s", kid);

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(msg, msg_len);
    if (!msg_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/pqc/mldsa/sign", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char sig_b64[6272] = {0};  /* ceil(4627 * 4/3) + 4 padding */
    hem_json_get_str(root, "sign", sig_b64, sizeof(sig_b64));
    cJSON_Delete(root);

    if (!sig_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing sign in mldsa/sign response");
        return HEM_ERR_JSON;
    }

    size_t dec_len = 0;
    uint8_t *dec = hem_base64_decode(sig_b64, &dec_len);
    if (!dec) return HEM_ERR_JSON;
    if (dec_len > sig_size) { free(dec); return HEM_ERR_BUFFER_TOO_SMALL; }
    memcpy(sig_out, dec, dec_len);
    free(dec);
    *sig_len = dec_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/pqc/mldsa/verify  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_mldsa_verify(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *msg,
                              size_t         msg_len,
                              const uint8_t *sig,
                              size_t         sig_len)
{
    if (!ctx || !kid || !msg || !sig)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    snprintf(scope, sizeof(scope), "keymgmt:use:%s", kid);

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(msg, msg_len);
    char *sig_b64 = hem_base64_encode(sig, sig_len);
    if (!msg_b64 || !sig_b64) {
        free(msg_b64); free(sig_b64);
        return HEM_ERR_JSON;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid",  kid);
    cJSON_AddStringToObject(req, "msg",  msg_b64);
    cJSON_AddStringToObject(req, "sign", sig_b64);
    free(msg_b64);
    free(sig_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/pqc/mldsa/verify", body, ctx->token);
    free(body);
    return err;  /* HEM_OK = valid; HEM_ERR_HTTP_STATUS (401/403) = invalid */
}
