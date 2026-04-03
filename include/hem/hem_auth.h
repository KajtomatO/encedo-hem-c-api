#ifndef HEM_AUTH_H
#define HEM_AUTH_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * hem_auth_ext_pair  --  Pair a phone app as an external authenticator.
 *
 * NOT YET FULLY IMPLEMENTED. Returns HEM_ERR_CHECKIN until the Encedo
 * notification broker URL and response format are confirmed.
 *
 * Intended protocol:
 *   1. POST /api/auth/ext/init   -- register session key with device
 *   2. POST <broker_url>         -- forward device response to notification
 *                                   broker (URL TBD -- see OPEN-QUESTIONS.md)
 *   3. POST /api/auth/ext/validate  -- confirm pairing with broker reply
 *
 * Parameters:
 *   epk_b64           Ephemeral X25519 session public key, standard base64.
 *   confirmation_out  Buffer to receive the opaque confirmation string.
 *   confirmation_size Size of confirmation_out (recommend >= 512 bytes).
 */
hem_error_t hem_auth_ext_pair(hem_ctx_t *ctx, const char *epk_b64,
                               char *confirmation_out, size_t confirmation_size);

/*
 * hem_auth_ext_login  --  Authenticate using the paired phone app.
 *
 * NOT YET FULLY IMPLEMENTED. Returns HEM_ERR_CHECKIN until the Encedo
 * notification broker URL and response format are confirmed.
 *
 * Intended protocol:
 *   1. POST /api/auth/ext/request  -- get challenge from device
 *   2. POST <broker_url>           -- forward challenge to notification
 *                                     broker (URL TBD -- see OPEN-QUESTIONS.md)
 *   3. POST /api/auth/ext/token    -- exchange broker reply for JWT token
 *
 * Parameters:
 *   epk_b64  Ephemeral X25519 session public key, standard base64.
 *   scope    Requested JWT scope string (e.g. "system:config", "keymgmt:gen").
 */
hem_error_t hem_auth_ext_login(hem_ctx_t *ctx, const char *epk_b64,
                                const char *scope);

#ifdef __cplusplus
}
#endif

#endif /* HEM_AUTH_H */
