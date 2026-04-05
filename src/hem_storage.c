#include <stdlib.h>
#include <string.h>

#include "hem/hem.h"
#include "hem/hem_storage.h"
#include "internal.h"

/* -------------------------------------------------------------------------
 * GET /api/storage/unlock
 * The scope encodes both the disk index and the access mode.
 * ---------------------------------------------------------------------- */
hem_error_t hem_storage_unlock(hem_ctx_t *ctx, const char *scope)
{
    if (!ctx || !scope || !scope[0]) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    return hem_http_get(ctx, "/api/storage/unlock", ctx->token);
}

/* -------------------------------------------------------------------------
 * GET /api/storage/lock
 * ---------------------------------------------------------------------- */
hem_error_t hem_storage_lock(hem_ctx_t *ctx, const char *scope)
{
    if (!ctx || !scope || !scope[0]) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, scope);
    if (err != HEM_OK) return err;

    return hem_http_get(ctx, "/api/storage/lock", ctx->token);
}
