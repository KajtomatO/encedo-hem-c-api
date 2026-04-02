#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "hem/hem.h"
#include "internal.h"

hem_ctx_t *hem_ctx_create(const char *base_url)
{
    if (!base_url) return NULL;

    /* Initialise libcurl globally (safe to call multiple times) */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    hem_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    strncpy(ctx->base_url, base_url, sizeof(ctx->base_url) - 1);
    /* Strip trailing slash for consistent URL construction */
    size_t len = strlen(ctx->base_url);
    while (len > 0 && ctx->base_url[len - 1] == '/') {
        ctx->base_url[--len] = '\0';
    }

    ctx->curl = curl_easy_init();
    if (!ctx->curl) {
        free(ctx);
        return NULL;
    }

    ctx->token_exp = 0;
    ctx->last_error = HEM_OK;

    return ctx;
}

hem_error_t hem_ctx_set_credentials(hem_ctx_t *ctx, const char *passphrase, hem_role_t role)
{
    if (!ctx || !passphrase) return HEM_ERR_INVALID_ARG;

    strncpy(ctx->passphrase, passphrase, sizeof(ctx->passphrase) - 1);
    ctx->passphrase[sizeof(ctx->passphrase) - 1] = '\0';
    ctx->role = (int)role;

    /* Invalidate any cached token since credentials changed */
    ctx->token[0] = '\0';
    ctx->token_exp = 0;

    return HEM_OK;
}

void hem_ctx_destroy(hem_ctx_t *ctx)
{
    if (!ctx) return;

    /* Zero passphrase before freeing */
    explicit_bzero(ctx->passphrase, sizeof(ctx->passphrase));

    if (ctx->curl) {
        curl_easy_cleanup(ctx->curl);
        ctx->curl = NULL;
    }

    free(ctx->resp_buf);
    free(ctx);
}

/* -------------------------------------------------------------------------
 * Error inspection
 * ---------------------------------------------------------------------- */

hem_error_t hem_last_error(const hem_ctx_t *ctx)
{
    return ctx ? ctx->last_error : HEM_ERR_INVALID_ARG;
}

int hem_last_http_status(const hem_ctx_t *ctx)
{
    return ctx ? ctx->http_status : 0;
}

const char *hem_last_error_msg(const hem_ctx_t *ctx)
{
    return ctx ? ctx->error_msg : "NULL context";
}

const char *hem_error_string(hem_error_t err)
{
    switch (err) {
        case HEM_OK:                  return "OK";
        case HEM_ERR_INVALID_ARG:     return "invalid argument";
        case HEM_ERR_HTTP:            return "HTTP transport error";
        case HEM_ERR_HTTP_STATUS:     return "HTTP error status";
        case HEM_ERR_JSON:            return "JSON parse error";
        case HEM_ERR_AUTH:            return "authentication failed";
        case HEM_ERR_DEVICE_FAILURE:  return "device in failure state";
        case HEM_ERR_BUFFER_TOO_SMALL:return "output buffer too small";
        case HEM_ERR_OPENSSL:         return "OpenSSL error";
        case HEM_ERR_CHECKIN:         return "check-in failed";
        default:                      return "unknown error";
    }
}
