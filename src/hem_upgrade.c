#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* sleep() */

#include "hem/hem.h"
#include "hem/hem_upgrade.h"
#include "internal.h"

#define UPGRADE_POLL_INTERVAL_S  2
#define UPGRADE_MAX_POLLS       120   /* 240 s total -- see OQ-10 */

/* -------------------------------------------------------------------------
 * Generic polling helper for check_fw / check_ui.
 * Polls `path` every UPGRADE_POLL_INTERVAL_S seconds until 200 is returned.
 * Returns HEM_OK when complete, HEM_ERR_HTTP_STATUS on hard errors or timeout.
 * ---------------------------------------------------------------------- */
static hem_error_t upgrade_poll(hem_ctx_t *ctx, const char *path)
{
    for (int i = 0; i < UPGRADE_MAX_POLLS; i++) {
        hem_error_t err = hem_http_get(ctx, path, ctx->token);

        if (err == HEM_OK)
            return HEM_OK;

        /* 202 Accepted: verification still in progress -- keep polling */
        if (err == HEM_ERR_HTTP_STATUS && ctx->http_status == 202) {
            if (i + 1 < UPGRADE_MAX_POLLS)
                sleep(UPGRADE_POLL_INTERVAL_S);
            continue;
        }

        /* Any other error (4xx, 5xx, transport) -- return immediately */
        return err;
    }

    hem_set_error(ctx, HEM_ERR_HTTP_STATUS, "upgrade check timed out after 240 seconds");
    return HEM_ERR_HTTP_STATUS;
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/usbmode  (scope: system:upgrade)
 * ---------------------------------------------------------------------- */
hem_error_t hem_upgrade_usbmode(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:upgrade");
    if (err != HEM_OK) return err;

    return hem_http_get(ctx, "/api/system/upgrade/usbmode", ctx->token);
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/check_fw  (scope: system:upgrade, polls)
 * ---------------------------------------------------------------------- */
hem_error_t hem_upgrade_check_fw(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:upgrade");
    if (err != HEM_OK) return err;

    return upgrade_poll(ctx, "/api/system/upgrade/check_fw");
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/install_fw  (scope: system:upgrade)
 * ---------------------------------------------------------------------- */
hem_error_t hem_upgrade_install_fw(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:upgrade");
    if (err != HEM_OK) return err;

    err = hem_http_get(ctx, "/api/system/upgrade/install_fw", ctx->token);
    if (err != HEM_OK) return err;

    /* Invalidate cached token -- device reboots after this call */
    ctx->token[0]       = '\0';
    ctx->token_scope[0] = '\0';
    ctx->token_exp      = 0;

    return HEM_OK;
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/check_ui  (scope: system:upgrade, polls)
 * ---------------------------------------------------------------------- */
hem_error_t hem_upgrade_check_ui(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:upgrade");
    if (err != HEM_OK) return err;

    return upgrade_poll(ctx, "/api/system/upgrade/check_ui");
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/install_ui  (scope: system:upgrade)
 * ---------------------------------------------------------------------- */
hem_error_t hem_upgrade_install_ui(hem_ctx_t *ctx)
{
    if (!ctx) return HEM_ERR_INVALID_ARG;

    hem_error_t err = hem_auth_ensure(ctx, "system:upgrade");
    if (err != HEM_OK) return err;

    return hem_http_get(ctx, "/api/system/upgrade/install_ui", ctx->token);
}
