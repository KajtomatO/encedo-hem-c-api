#ifndef HEM_SYSTEM_H
#define HEM_SYSTEM_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GET /api/system/version
 * No authentication required.
 * Fills *out with hardware/firmware version info.
 */
hem_error_t hem_system_version(hem_ctx_t *ctx, hem_version_t *out);

/*
 * GET /api/system/status
 * No authentication required.
 * Fills *out with current device operational status.
 */
hem_error_t hem_system_status(hem_ctx_t *ctx, hem_status_t *out);

/*
 * Perform the two-phase check-in procedure:
 *   1. GET  /api/system/checkin          -- get challenge
 *   2. POST https://api.encedo.com/checkin -- forward to backend
 *   3. POST /api/system/checkin          -- complete with backend response
 *
 * No authentication required.
 * Sets the device RTC clock as a side-effect.
 */
hem_error_t hem_system_checkin(hem_ctx_t *ctx);

/*
 * GET /api/system/config
 * Requires authentication with scope "system:config".
 * Authenticates automatically if no valid token is cached.
 */
hem_error_t hem_system_config(hem_ctx_t *ctx, hem_config_t *out);

/*
 * POST /api/system/config
 * Scope: system:config  (authenticated automatically)
 *
 * Updates device configuration.  At least one field must be non-NULL.
 * `wipeout` is intentionally not exposed -- use hem_system_wipeout() if added later.
 *
 * user_name  new user display name (max 64 chars), or NULL to leave unchanged
 * tls_json   TLS certificate object as a JSON string, or NULL to leave unchanged
 * result     receives updated/reboot_required flags; may be NULL
 */
hem_error_t hem_system_config_set(hem_ctx_t           *ctx,
                                   const char          *user_name,
                                   const char          *tls_json,
                                   hem_config_update_t *result);

/*
 * GET /api/system/reboot
 * Scope: system:config  (authenticated automatically)
 *
 * Reboots the device.  The device will go offline for ~10-30 seconds.
 * All active sessions and cached tokens are invalidated.
 */
hem_error_t hem_system_reboot(hem_ctx_t *ctx);

/*
 * GET /api/system/shutdown
 * Scope: system:config  (authenticated automatically)
 *
 * Powers off the device (PPA only).  Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 * After this call the device is unreachable until physically powered on again.
 */
hem_error_t hem_system_shutdown(hem_ctx_t *ctx);

/*
 * GET /api/system/selftest  (polls until complete)
 * Scope: system:config  (authenticated automatically)
 *
 * Triggers the Known Answer Tests (KAT) self-test and polls until completion.
 * May take up to 240 seconds.  Polls every 2 seconds, times out after 120 polls.
 *
 * fls_state_out  receives the final fls_state value (0 = all tests passed);
 *                may be NULL if caller does not need it
 */
hem_error_t hem_system_selftest(hem_ctx_t *ctx, int *fls_state_out);

/*
 * GET /api/system/config/attestation
 * Auth: any valid token  (authenticated automatically with system:config scope)
 *
 * Returns the device attestation blob (PPA only).
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 * Returns HEM_ERR_DEVICE_FAILURE (409) if device is in failure state.
 *
 * genuine_out   caller buffer for the opaque base64 attestation blob
 * genuine_size  size of buffer (recommend >= 4096 bytes)
 */
hem_error_t hem_system_attestation(hem_ctx_t *ctx,
                                    char      *genuine_out,
                                    size_t     genuine_size);

/*
 * POST /api/system/config/provisioning
 * Scope: system:config  (authenticated automatically)
 *
 * Provisions the device with its TLS certificate (PPA only, one-time).
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 * Returns HEM_ERR_AUTH (403) if device is already provisioned.
 *
 * crt      Certificate data string from the Encedo backend.
 * genuine  Attestation blob returned by hem_system_attestation().
 *
 * WARNING: This operation is IRREVERSIBLE.
 */
hem_error_t hem_system_provision(hem_ctx_t  *ctx,
                                  const char *crt,
                                  const char *genuine);

#ifdef __cplusplus
}
#endif

#endif /* HEM_SYSTEM_H */
