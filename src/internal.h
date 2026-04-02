#ifndef HEM_INTERNAL_H
#define HEM_INTERNAL_H

#include <curl/curl.h>
#include <time.h>
#include <string.h>
#include "hem/hem_types.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * Full context definition (opaque to library users)
 * ---------------------------------------------------------------------- */
struct hem_ctx {
    /* Connection */
    char    base_url[256];
    CURL   *curl;

    /* Credentials (set via hem_ctx_set_credentials) */
    char    passphrase[256];
    int     role;           /* hem_role_t */

    /* Device auth state cached from GET /api/auth/token */
    char    eid[128];       /* Entity ID -- used as PBKDF2 salt */
    char    spk[128];       /* Device X25519 public key (base64) */

    /* Token cache -- one active token at a time */
    char    token[2048];
    char    token_scope[128];
    time_t  token_exp;

    /* Last error state */
    hem_error_t last_error;
    int         http_status;
    char        error_msg[256];

    /* HTTP response buffer (grown dynamically) */
    char   *resp_buf;
    size_t  resp_len;
    size_t  resp_cap;
};

/* -------------------------------------------------------------------------
 * Internal HTTP functions
 * ---------------------------------------------------------------------- */

/*
 * Perform GET/POST/DELETE against a path relative to ctx->base_url.
 * token may be NULL for unauthenticated requests.
 * body is the JSON request body for POST (NULL for GET/DELETE).
 * On return, ctx->resp_buf contains the null-terminated response body.
 */
hem_error_t hem_http_get   (hem_ctx_t *ctx, const char *path, const char *token);
hem_error_t hem_http_post  (hem_ctx_t *ctx, const char *path, const char *body, const char *token);
hem_error_t hem_http_delete(hem_ctx_t *ctx, const char *path, const char *token);

/*
 * Like hem_http_post but to an absolute URL (used for check-in backend call).
 */
hem_error_t hem_http_post_url(hem_ctx_t *ctx, const char *url, const char *body);

/* -------------------------------------------------------------------------
 * Internal JSON helpers
 * ---------------------------------------------------------------------- */

/* Parse ctx->resp_buf as JSON. Caller must cJSON_Delete() the result. */
cJSON *hem_json_parse_response(hem_ctx_t *ctx);

/* Safe string extraction -- writes "" if key absent or not a string */
void hem_json_get_str(const cJSON *obj, const char *key, char *out, size_t out_size);

/* Integer extraction -- returns def_val if key absent or not a number */
int64_t hem_json_get_int64(const cJSON *obj, const char *key, int64_t def_val);

/* Boolean extraction -- returns def_val if key absent */
bool hem_json_get_bool(const cJSON *obj, const char *key, bool def_val);

/* Base64 standard encode/decode (for API message payloads) */
char   *hem_base64_encode(const uint8_t *data, size_t len);   /* caller free()s result */
uint8_t *hem_base64_decode(const char *b64, size_t *out_len); /* caller free()s result */

/* -------------------------------------------------------------------------
 * Internal auth function
 * ---------------------------------------------------------------------- */

/*
 * Ensure a valid JWT token exists for the given scope.
 * Re-authenticates automatically if the cached token is absent, has the wrong
 * scope, or is within TOKEN_REFRESH_MARGIN seconds of expiry.
 */
hem_error_t hem_auth_ensure(hem_ctx_t *ctx, const char *scope);

/* -------------------------------------------------------------------------
 * Error helpers
 * ---------------------------------------------------------------------- */

static inline void hem_set_error(hem_ctx_t *ctx, hem_error_t err, const char *msg)
{
    ctx->last_error = err;
    strncpy(ctx->error_msg, msg ? msg : "", sizeof(ctx->error_msg) - 1);
    ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
}

#endif /* HEM_INTERNAL_H */
