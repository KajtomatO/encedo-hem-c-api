#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "hem/hem.h"
#include "internal.h"
#include "cJSON.h"

/* Re-authenticate if the cached token expires within this many seconds */
#define TOKEN_REFRESH_MARGIN 60

/* -------------------------------------------------------------------------
 * base64url encoding
 * JWT requires base64url (RFC 4648 §5): no padding, '+' -> '-', '/' -> '_'
 * ---------------------------------------------------------------------- */
static char *b64url_encode(const uint8_t *data, size_t len)
{
    char *b64 = hem_base64_encode(data, len);
    if (!b64) return NULL;

    for (char *p = b64; *p; p++) {
        if      (*p == '+') *p = '-';
        else if (*p == '/') *p = '_';
        else if (*p == '=') { *p = '\0'; break; } /* strip padding */
    }
    return b64;
}

/* -------------------------------------------------------------------------
 * build_ejwt
 *
 * Constructs a signed eJWT for the HEM authentication protocol:
 *
 *   seed   = PBKDF2-SHA256(passphrase, eid, 600000 iterations, 32 bytes)
 *   pubkey = X25519_pubkey(seed)           -- user identity
 *   shared = X25519(seed, device_spk)      -- HMAC key
 *   header = {"ecdh":"x25519","alg":"HS256","typ":"JWT"}
 *   payload = {jti, aud=spk, exp, iat, iss=pubkey_b64, scope}
 *   sig    = HMAC-SHA256(shared, b64url(header) + "." + b64url(payload))
 *   ejwt   = b64url(header) + "." + b64url(payload) + "." + b64url(sig)
 * ---------------------------------------------------------------------- */
static hem_error_t build_ejwt(
    const char *passphrase,
    const char *eid,       /* PBKDF2 salt (device entity ID) */
    const char *spk_b64,   /* device X25519 public key, standard base64 */
    const char *jti,       /* challenge nonce */
    int64_t     req_exp,   /* requested token expiry (Unix timestamp) */
    const char *scope,     /* requested JWT scope string */
    char       *ejwt_out,
    size_t      ejwt_size)
{
    hem_error_t result = HEM_ERR_OPENSSL;

    uint8_t  seed[32]   = {0};
    uint8_t  shared[32] = {0};
    uint8_t  pubkey[32] = {0};
    size_t   pubkey_len = 32;
    size_t   shared_len = 32;

    EVP_PKEY     *privkey  = NULL;
    EVP_PKEY     *peerkey  = NULL;
    EVP_PKEY_CTX *kctx     = NULL;
    uint8_t      *spk_raw  = NULL;
    size_t        spk_len  = 0;
    char         *iss_b64  = NULL;
    char         *h        = NULL;
    char         *p        = NULL;
    char         *s        = NULL;
    char         *signing  = NULL;

    /* 1. PBKDF2-SHA256(passphrase, eid, 600000) -> 32-byte seed */
    if (!PKCS5_PBKDF2_HMAC(passphrase, (int)strlen(passphrase),
                            (unsigned char *)eid, (int)strlen(eid),
                            600000, EVP_sha256(), 32, seed))
        goto cleanup;

    /* 2. seed IS the X25519 private key scalar; derive the public key */
    privkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, seed, 32);
    if (!privkey) goto cleanup;

    if (!EVP_PKEY_get_raw_public_key(privkey, pubkey, &pubkey_len))
        goto cleanup;

    /* 3. Decode device spk from standard base64 to raw 32 bytes */
    spk_raw = hem_base64_decode(spk_b64, &spk_len);
    if (!spk_raw || spk_len != 32) goto cleanup;

    /* 4. ECDH: shared_secret = X25519(privkey, device_spk) */
    peerkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, spk_raw, 32);
    if (!peerkey) goto cleanup;

    kctx = EVP_PKEY_CTX_new(privkey, NULL);
    if (!kctx) goto cleanup;
    if (EVP_PKEY_derive_init(kctx) <= 0)          goto cleanup;
    if (EVP_PKEY_derive_set_peer(kctx, peerkey) <= 0) goto cleanup;
    if (EVP_PKEY_derive(kctx, shared, &shared_len) <= 0) goto cleanup;

    /* 5. Encode user public key in standard base64 for the iss claim */
    iss_b64 = hem_base64_encode(pubkey, 32);
    if (!iss_b64) goto cleanup;

    /* 6. Build and base64url-encode the header (fixed for all eJWTs) */
    const char *hdr_json =
        "{\"ecdh\":\"x25519\",\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    h = b64url_encode((uint8_t *)hdr_json, strlen(hdr_json));
    if (!h) goto cleanup;

    /* 7. Build and base64url-encode the payload */
    char payload_json[768];
    int64_t iat = (int64_t)time(NULL);
    int n = snprintf(payload_json, sizeof(payload_json),
        "{\"jti\":\"%s\",\"aud\":\"%s\","
        "\"exp\":%lld,\"iat\":%lld,"
        "\"iss\":\"%s\",\"scope\":\"%s\"}",
        jti, spk_b64,
        (long long)req_exp, (long long)iat,
        iss_b64, scope);
    if (n < 0 || (size_t)n >= sizeof(payload_json)) goto cleanup;

    p = b64url_encode((uint8_t *)payload_json, (size_t)n);
    if (!p) goto cleanup;

    /* 8. signing_input = base64url(header) + "." + base64url(payload) */
    size_t h_len = strlen(h);
    size_t p_len = strlen(p);
    signing = malloc(h_len + 1 + p_len + 1);
    if (!signing) goto cleanup;
    memcpy(signing, h, h_len);
    signing[h_len] = '.';
    memcpy(signing + h_len + 1, p, p_len);
    signing[h_len + 1 + p_len] = '\0';

    /* 9. signature = HMAC-SHA256(shared_secret, signing_input) */
    uint8_t      sig[32];
    unsigned int sig_len = 32;
    if (!HMAC(EVP_sha256(), shared, (int)shared_len,
              (unsigned char *)signing, h_len + 1 + p_len,
              sig, &sig_len))
        goto cleanup;

    s = b64url_encode(sig, sig_len);
    if (!s) goto cleanup;

    /* 10. Assemble: header.payload.signature */
    n = snprintf(ejwt_out, ejwt_size, "%s.%s.%s", h, p, s);
    if (n < 0 || (size_t)n >= ejwt_size) goto cleanup;

    result = HEM_OK;

cleanup:
    /* Zero all sensitive key material before freeing */
    explicit_bzero(seed,   sizeof(seed));
    explicit_bzero(shared, sizeof(shared));
    explicit_bzero(pubkey, sizeof(pubkey));

    EVP_PKEY_CTX_free(kctx);
    EVP_PKEY_free(peerkey);
    EVP_PKEY_free(privkey);
    free(spk_raw);
    free(iss_b64);
    free(h);
    free(p);
    free(s);
    free(signing);
    return result;
}

/* -------------------------------------------------------------------------
 * hem_auth_login
 * Full eJWT challenge-response authentication for a given scope.
 * Caches the resulting JWT token in the context.
 * ---------------------------------------------------------------------- */
hem_error_t hem_auth_login(hem_ctx_t *ctx, const char *scope)
{
    if (!ctx || !scope) return HEM_ERR_INVALID_ARG;

    if (ctx->passphrase[0] == '\0') {
        hem_set_error(ctx, HEM_ERR_AUTH,
                      "no credentials set -- call hem_ctx_set_credentials first");
        return HEM_ERR_AUTH;
    }

    /* --- Step 1: GET /api/auth/token to receive the challenge --- */
    hem_error_t err = hem_http_get(ctx, "/api/auth/token", NULL);
    if (err != HEM_OK) return err;

    cJSON *root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char    eid[128] = {0};
    char    spk[128] = {0};
    char    jti[128] = {0};
    int64_t exp_challenge = 0;

    hem_json_get_str(root, "eid", eid, sizeof(eid));
    hem_json_get_str(root, "spk", spk, sizeof(spk));
    hem_json_get_str(root, "jti", jti, sizeof(jti));
    exp_challenge = hem_json_get_int64(root, "exp", 0);
    cJSON_Delete(root);

    if (!eid[0] || !spk[0] || !jti[0]) {
        hem_set_error(ctx, HEM_ERR_JSON, "auth challenge missing eid/spk/jti");
        return HEM_ERR_JSON;
    }

    /* Cache eid/spk in context for future use */
    strncpy(ctx->eid, eid, sizeof(ctx->eid) - 1);
    strncpy(ctx->spk, spk, sizeof(ctx->spk) - 1);

    /*
     * Requested token lifetime: 1 hour from now.
     * Cap at the challenge's exp if the device imposes a shorter limit.
     */
    int64_t req_exp = (int64_t)time(NULL) + 3600;
    if (exp_challenge > 0 && exp_challenge < req_exp)
        req_exp = exp_challenge;

    /* --- Step 2: construct the eJWT --- */
    char ejwt[2048] = {0};
    err = build_ejwt(ctx->passphrase, eid, spk, jti, req_exp, scope,
                     ejwt, sizeof(ejwt));
    if (err != HEM_OK) {
        hem_set_error(ctx, HEM_ERR_OPENSSL, "eJWT construction failed");
        return err;
    }

    /* --- Step 3: POST /api/auth/token {"auth": "<ejwt>"} --- */
    char body[2300];
    snprintf(body, sizeof(body), "{\"auth\":\"%s\"}", ejwt);
    explicit_bzero(ejwt, sizeof(ejwt));

    err = hem_http_post(ctx, "/api/auth/token", body, NULL);
    if (err != HEM_OK) return err;

    root = hem_json_parse_response(ctx);
    if (!root) return HEM_ERR_JSON;

    char token[2048] = {0};
    hem_json_get_str(root, "token", token, sizeof(token));
    cJSON_Delete(root);

    if (!token[0]) {
        hem_set_error(ctx, HEM_ERR_AUTH, "no token in authentication response");
        return HEM_ERR_AUTH;
    }

    /* --- Cache the token --- */
    strncpy(ctx->token, token, sizeof(ctx->token) - 1);
    strncpy(ctx->token_scope, scope, sizeof(ctx->token_scope) - 1);
    ctx->token_exp   = (time_t)req_exp;
    ctx->last_error  = HEM_OK;
    ctx->error_msg[0] = '\0';

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * hem_auth_ensure  (internal)
 * Returns a valid token for the requested scope, re-authenticating if the
 * cached token is absent, has a different scope, or is about to expire.
 * ---------------------------------------------------------------------- */
hem_error_t hem_auth_ensure(hem_ctx_t *ctx, const char *scope)
{
    if (!ctx || !scope) return HEM_ERR_INVALID_ARG;

    if (ctx->token[0] != '\0' &&
        strcmp(ctx->token_scope, scope) == 0 &&
        (int64_t)time(NULL) < (int64_t)ctx->token_exp - TOKEN_REFRESH_MARGIN)
    {
        return HEM_OK; /* cached token is still valid */
    }

    return hem_auth_login(ctx, scope);
}
