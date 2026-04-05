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

/*
 * hem_auth_device_init  --  Factory initialisation (one-time, irreversible).
 *
 * Two-phase protocol:
 *   Phase 1: GET  /api/auth/init  -- obtain challenge (eid, spk, jti, exp)
 *   Phase 2: POST /api/auth/init  -- submit signed init eJWT with cfg payload
 *
 * master_pass  Master (admin) passphrase.  The X25519 public key derived from
 *              this passphrase becomes the device master key.
 * user_pass    User passphrase.  The X25519 public key becomes the user key.
 * user_name    User display name.
 * email        User email address.
 * hostname     Device FQDN (e.g. "mydevice.ence.do").
 * opts         Optional configuration fields; pass NULL for defaults.
 * result       Receives instanceid, initial token, CSR and attestation blob.
 *              May be NULL if caller does not need the response fields.
 *
 * Returns HEM_ERR_HTTP_STATUS (406) if the device is already initialised.
 *
 * WARNING: This operation is IRREVERSIBLE without a factory reset.
 *          Only call on a fresh / unprovisioned device.
 */
hem_error_t hem_auth_device_init(hem_ctx_t               *ctx,
                                  const char              *master_pass,
                                  const char              *user_pass,
                                  const char              *user_name,
                                  const char              *email,
                                  const char              *hostname,
                                  const hem_init_config_t *opts,
                                  hem_init_result_t       *result);

#ifdef __cplusplus
}
#endif

#endif /* HEM_AUTH_H */
