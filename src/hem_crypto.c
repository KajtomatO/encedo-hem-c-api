#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hem/hem.h"
#include "hem/hem_crypto.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * Build scope string "keymgmt:use:<kid>" into buf.
 * buf must be at least 13 + 32 + 1 = 46 bytes.
 * ---------------------------------------------------------------------- */
static void make_use_scope(const char *kid, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "keymgmt:use:%s", kid);
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/cipher/encrypt  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_encrypt(hem_ctx_t           *ctx,
                         const char          *kid,
                         const char          *alg,
                         const uint8_t       *plaintext,
                         size_t               pt_len,
                         const uint8_t       *aad,
                         size_t               aad_len,
                         uint8_t             *ct_buf,
                         size_t               ct_buf_size,
                         hem_cipher_result_t *result)
{
    if (!ctx || !kid || !alg || !plaintext || !ct_buf || !result)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    /* base64-encode plaintext for the msg field */
    char *msg_b64 = hem_base64_encode(plaintext, pt_len);
    if (!msg_b64) return HEM_ERR_JSON;

    /* Build request JSON */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "alg", alg);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);

    if (aad && aad_len > 0) {
        char *aad_b64 = hem_base64_encode(aad, aad_len);
        if (aad_b64) {
            cJSON_AddStringToObject(req, "aad", aad_b64);
            free(aad_b64);
        }
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/cipher/encrypt", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    /* Parse response: {"ciphertext": "<b64>", "iv": "<b64>", "tag": "<b64>"} */
    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char ct_b64[8192]  = {0};
    char iv_b64[64]    = {0};
    char tag_b64[64]   = {0};

    hem_json_get_str(root, "ciphertext", ct_b64,  sizeof(ct_b64));
    hem_json_get_str(root, "iv",         iv_b64,   sizeof(iv_b64));
    hem_json_get_str(root, "tag",        tag_b64,  sizeof(tag_b64));
    cJSON_Delete(root);

    if (!ct_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing ciphertext in encrypt response");
        return HEM_ERR_JSON;
    }

    /* Decode ciphertext into caller buffer */
    size_t ct_len = 0;
    uint8_t *ct_decoded = hem_base64_decode(ct_b64, &ct_len);
    if (!ct_decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode ciphertext");
        return HEM_ERR_JSON;
    }
    if (ct_len > ct_buf_size) {
        free(ct_decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "ct_buf too small for ciphertext");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(ct_buf, ct_decoded, ct_len);
    free(ct_decoded);

    /* Decode IV */
    result->iv_len = 0;
    if (iv_b64[0]) {
        size_t iv_len = 0;
        uint8_t *iv_decoded = hem_base64_decode(iv_b64, &iv_len);
        if (iv_decoded) {
            size_t copy = iv_len < sizeof(result->iv) ? iv_len : sizeof(result->iv);
            memcpy(result->iv, iv_decoded, copy);
            result->iv_len = copy;
            free(iv_decoded);
        }
    }

    /* Decode GCM auth tag */
    result->tag_len = 0;
    if (tag_b64[0]) {
        size_t tag_len = 0;
        uint8_t *tag_decoded = hem_base64_decode(tag_b64, &tag_len);
        if (tag_decoded) {
            size_t copy = tag_len < sizeof(result->tag) ? tag_len : sizeof(result->tag);
            memcpy(result->tag, tag_decoded, copy);
            result->tag_len = copy;
            free(tag_decoded);
        }
    }

    result->ciphertext     = ct_buf;
    result->ciphertext_len = ct_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/cipher/decrypt  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_decrypt(hem_ctx_t     *ctx,
                         const char    *kid,
                         const char    *alg,
                         const uint8_t *ciphertext,
                         size_t         ct_len,
                         const uint8_t *iv,
                         size_t         iv_len,
                         const uint8_t *tag,
                         size_t         tag_len,
                         const uint8_t *aad,
                         size_t         aad_len,
                         uint8_t       *pt_buf,
                         size_t         pt_buf_size,
                         size_t        *pt_out_len)
{
    if (!ctx || !kid || !alg || !ciphertext || !pt_buf || !pt_out_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    /* base64-encode inputs */
    char *ct_b64  = hem_base64_encode(ciphertext, ct_len);
    char *iv_b64  = (iv  && iv_len)  ? hem_base64_encode(iv,  iv_len)  : NULL;
    char *tag_b64 = (tag && tag_len) ? hem_base64_encode(tag, tag_len) : NULL;
    char *aad_b64 = (aad && aad_len) ? hem_base64_encode(aad, aad_len) : NULL;

    if (!ct_b64) {
        free(iv_b64); free(tag_b64); free(aad_b64);
        return HEM_ERR_JSON;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "alg", alg);
    cJSON_AddStringToObject(req, "msg", ct_b64);
    if (iv_b64)  cJSON_AddStringToObject(req, "iv",  iv_b64);
    if (tag_b64) cJSON_AddStringToObject(req, "tag", tag_b64);
    if (aad_b64) cJSON_AddStringToObject(req, "aad", aad_b64);

    free(ct_b64); free(iv_b64); free(tag_b64); free(aad_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/cipher/decrypt", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    /* Parse response: {"plaintext": "<b64>"} */
    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char pt_b64[8192] = {0};
    hem_json_get_str(root, "plaintext", pt_b64, sizeof(pt_b64));
    cJSON_Delete(root);

    if (!pt_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing plaintext in decrypt response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(pt_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode plaintext");
        return HEM_ERR_JSON;
    }
    if (decoded_len > pt_buf_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "pt_buf too small for plaintext");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(pt_buf, decoded, decoded_len);
    free(decoded);
    *pt_out_len = decoded_len;

    return HEM_OK;
}
