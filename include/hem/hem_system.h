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

#ifdef __cplusplus
}
#endif

#endif /* HEM_SYSTEM_H */
