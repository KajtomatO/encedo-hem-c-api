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

/* -------------------------------------------------------------------------
 * POST /api/crypto/cipher/wrap  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_wrap(hem_ctx_t     *ctx,
                          const char    *kid,
                          const char    *alg,
                          const uint8_t *key_material,
                          size_t         key_len,
                          uint8_t       *wrapped_out,
                          size_t         wrapped_size,
                          size_t        *wrapped_len)
{
    if (!ctx || !kid || !alg || !key_material || !wrapped_out || !wrapped_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(key_material, key_len);
    if (!msg_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "alg", alg);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/cipher/wrap", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char wrapped_b64[4096] = {0};
    hem_json_get_str(root, "wrapped", wrapped_b64, sizeof(wrapped_b64));
    cJSON_Delete(root);

    if (!wrapped_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing wrapped in wrap response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(wrapped_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode wrapped key");
        return HEM_ERR_JSON;
    }
    if (decoded_len > wrapped_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "wrapped_out too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(wrapped_out, decoded, decoded_len);
    free(decoded);
    *wrapped_len = decoded_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/cipher/unwrap  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_key_unwrap(hem_ctx_t     *ctx,
                            const char    *kid,
                            const char    *alg,
                            const uint8_t *wrapped,
                            size_t         wrapped_len,
                            uint8_t       *key_out,
                            size_t         key_size,
                            size_t        *key_len)
{
    if (!ctx || !kid || !alg || !wrapped || !key_out || !key_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(wrapped, wrapped_len);
    if (!msg_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "alg", alg);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/cipher/unwrap", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char unwrapped_b64[4096] = {0};
    hem_json_get_str(root, "unwrapped", unwrapped_b64, sizeof(unwrapped_b64));
    cJSON_Delete(root);

    if (!unwrapped_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing unwrapped in unwrap response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(unwrapped_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode unwrapped key");
        return HEM_ERR_JSON;
    }
    if (decoded_len > key_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "key_out too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(key_out, decoded, decoded_len);
    free(decoded);
    *key_len = decoded_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/hmac/hash  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_hmac_hash(hem_ctx_t     *ctx,
                           const char    *kid,
                           const char    *alg,
                           const uint8_t *msg,
                           size_t         msg_len,
                           uint8_t       *mac_out,
                           size_t         mac_size,
                           size_t        *mac_len)
{
    if (!ctx || !kid || !msg || !mac_out || !mac_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(msg, msg_len);
    if (!msg_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);
    if (alg) cJSON_AddStringToObject(req, "alg", alg);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/hmac/hash", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char mac_b64[256] = {0};
    hem_json_get_str(root, "mac", mac_b64, sizeof(mac_b64));
    cJSON_Delete(root);

    if (!mac_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing mac in hmac/hash response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(mac_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode mac");
        return HEM_ERR_JSON;
    }
    if (decoded_len > mac_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "mac_out too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(mac_out, decoded, decoded_len);
    free(decoded);
    *mac_len = decoded_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/hmac/verify  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_hmac_verify(hem_ctx_t     *ctx,
                             const char    *kid,
                             const char    *alg,
                             const uint8_t *msg,
                             size_t         msg_len,
                             const uint8_t *mac,
                             size_t         mac_len)
{
    if (!ctx || !kid || !msg || !mac)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(msg, msg_len);
    char *mac_b64 = hem_base64_encode(mac, mac_len);
    if (!msg_b64 || !mac_b64) {
        free(msg_b64); free(mac_b64);
        return HEM_ERR_JSON;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    cJSON_AddStringToObject(req, "mac", mac_b64);
    free(msg_b64);
    free(mac_b64);
    if (alg) cJSON_AddStringToObject(req, "alg", alg);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/hmac/verify", body, ctx->token);
    free(body);
    return err;  /* HEM_OK = valid; HEM_ERR_HTTP_STATUS (401/403) = invalid */
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/exdsa/sign  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_sign(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *alg,
                      const uint8_t *msg,
                      size_t         msg_len,
                      const uint8_t *sign_ctx,
                      size_t         sign_ctx_len,
                      uint8_t       *sig_out,
                      size_t         sig_size,
                      size_t        *sig_len)
{
    if (!ctx || !kid || !alg || !msg || !sig_out || !sig_len)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    char *msg_b64 = hem_base64_encode(msg, msg_len);
    if (!msg_b64) return HEM_ERR_JSON;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    cJSON_AddStringToObject(req, "alg", alg);
    cJSON_AddStringToObject(req, "msg", msg_b64);
    free(msg_b64);

    if (sign_ctx && sign_ctx_len > 0) {
        char *ctx_b64 = hem_base64_encode(sign_ctx, sign_ctx_len);
        if (ctx_b64) {
            cJSON_AddStringToObject(req, "ctx", ctx_b64);
            free(ctx_b64);
        }
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/exdsa/sign", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char sig_b64[8192] = {0};
    hem_json_get_str(root, "sign", sig_b64, sizeof(sig_b64));
    cJSON_Delete(root);

    if (!sig_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing sign in exdsa/sign response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(sig_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode signature");
        return HEM_ERR_JSON;
    }
    if (decoded_len > sig_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "sig_out too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(sig_out, decoded, decoded_len);
    free(decoded);
    *sig_len = decoded_len;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/exdsa/verify  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_verify(hem_ctx_t     *ctx,
                        const char    *kid,
                        const char    *alg,
                        const uint8_t *msg,
                        size_t         msg_len,
                        const uint8_t *sig,
                        size_t         sig_len,
                        const uint8_t *sign_ctx,
                        size_t         sign_ctx_len)
{
    if (!ctx || !kid || !alg || !msg || !sig)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

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
    cJSON_AddStringToObject(req, "alg",  alg);
    cJSON_AddStringToObject(req, "msg",  msg_b64);
    cJSON_AddStringToObject(req, "sign", sig_b64);
    free(msg_b64);
    free(sig_b64);

    if (sign_ctx && sign_ctx_len > 0) {
        char *ctx_b64 = hem_base64_encode(sign_ctx, sign_ctx_len);
        if (ctx_b64) {
            cJSON_AddStringToObject(req, "ctx", ctx_b64);
            free(ctx_b64);
        }
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/exdsa/verify", body, ctx->token);
    free(body);
    return err;  /* HEM_OK = valid; HEM_ERR_HTTP_STATUS (401/403) = invalid */
}

/* -------------------------------------------------------------------------
 * POST /api/crypto/ecdh  (scope: keymgmt:use:<kid>)
 * ---------------------------------------------------------------------- */
hem_error_t hem_ecdh(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *peer_pubkey_b64,
                      const char    *peer_kid,
                      const char    *alg,
                      uint8_t       *secret_out,
                      size_t         secret_size,
                      size_t        *secret_len)
{
    if (!ctx || !kid || !secret_out || !secret_len)
        return HEM_ERR_INVALID_ARG;
    if (!peer_pubkey_b64 && !peer_kid)
        return HEM_ERR_INVALID_ARG;

    char scope[64];
    make_use_scope(kid, scope, sizeof(scope));

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "kid", kid);
    if (peer_pubkey_b64)
        cJSON_AddStringToObject(req, "pubkey",  peer_pubkey_b64);
    if (peer_kid)
        cJSON_AddStringToObject(req, "ext_kid", peer_kid);
    if (alg)
        cJSON_AddStringToObject(req, "alg", alg);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return HEM_ERR_JSON;

    err = hem_http_post(ctx, "/api/crypto/ecdh", body, ctx->token);
    free(body);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char secret_b64[256] = {0};
    hem_json_get_str(root, "ecdh", secret_b64, sizeof(secret_b64));
    cJSON_Delete(root);

    if (!secret_b64[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "missing ecdh in ecdh response");
        return HEM_ERR_JSON;
    }

    size_t decoded_len = 0;
    uint8_t *decoded = hem_base64_decode(secret_b64, &decoded_len);
    if (!decoded) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to base64-decode ecdh secret");
        return HEM_ERR_JSON;
    }
    if (decoded_len > secret_size) {
        free(decoded);
        hem_set_error(ctx, HEM_ERR_BUFFER_TOO_SMALL, "secret_out too small");
        return HEM_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(secret_out, decoded, decoded_len);
    free(decoded);
    *secret_len = decoded_len;

    return HEM_OK;
}
