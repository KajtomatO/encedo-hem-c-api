#ifndef HEM_H
#define HEM_H

/*
 * hem.h -- Umbrella header for the Encedo HEM C client library.
 *
 * Include this single header to get the full public API.
 */

#include "hem_types.h"
#include "hem_system.h"
#include "hem_keymgmt.h"
#include "hem_crypto.h"
#include "hem_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Context lifecycle
 * ---------------------------------------------------------------------- */

/*
 * Create a new HEM context.
 * base_url: device base URL, e.g. "https://my.ence.do" (no trailing slash)
 * Returns NULL on allocation failure.
 */
hem_ctx_t *hem_ctx_create(const char *base_url);

/*
 * Set credentials used for automatic authentication.
 * passphrase: user/master passphrase (copied into context)
 * role:       HEM_ROLE_USER or HEM_ROLE_MASTER
 */
hem_error_t hem_ctx_set_credentials(hem_ctx_t *ctx, const char *passphrase, hem_role_t role);

/*
 * Destroy a context and free all associated resources.
 * Zeros the stored passphrase before freeing.
 */
void hem_ctx_destroy(hem_ctx_t *ctx);

/* -------------------------------------------------------------------------
 * Error inspection
 * ---------------------------------------------------------------------- */

hem_error_t hem_last_error(const hem_ctx_t *ctx);
int         hem_last_http_status(const hem_ctx_t *ctx);
const char *hem_last_error_msg(const hem_ctx_t *ctx);
const char *hem_error_string(hem_error_t err);

/* Explicit authentication -- called automatically by all API functions,
 * but exposed here for pre-authentication or scope verification. */
hem_error_t hem_auth_login(hem_ctx_t *ctx, const char *scope);

#ifdef __cplusplus
}
#endif

#endif /* HEM_H */
