#ifndef HEM_UPGRADE_H
#define HEM_UPGRADE_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GET /api/system/upgrade/usbmode
 * Scope: system:upgrade  (authenticated automatically)
 *
 * Activates USB DFU mode for firmware upgrade via serial (PPA only).
 * After this call the device switches to USB mode; further HTTP communication
 * is not possible until the device is rebooted.
 */
hem_error_t hem_upgrade_usbmode(hem_ctx_t *ctx);

/*
 * GET /api/system/upgrade/check_fw  (polls until complete)
 * Scope: system:upgrade  (authenticated automatically)
 *
 * Polls until the uploaded firmware image has been verified (200) or until
 * the timeout expires.  Returns 202 while verification is in progress.
 *
 * Polls every 2 seconds; times out after 120 polls (240 seconds).
 * See OQ-10 for the recommended polling interval rationale.
 */
hem_error_t hem_upgrade_check_fw(hem_ctx_t *ctx);

/*
 * GET /api/system/upgrade/install_fw
 * Scope: system:upgrade  (authenticated automatically)
 *
 * Installs the verified firmware and reboots the device.
 * The device will be offline for ~30-60 seconds after this call.
 * The cached token is invalidated on return.
 */
hem_error_t hem_upgrade_install_fw(hem_ctx_t *ctx);

/*
 * GET /api/system/upgrade/check_ui  (polls until complete)
 * Scope: system:upgrade  (authenticated automatically)
 *
 * Same polling behaviour as hem_upgrade_check_fw, for the UI archive.
 */
hem_error_t hem_upgrade_check_ui(hem_ctx_t *ctx);

/*
 * GET /api/system/upgrade/install_ui
 * Scope: system:upgrade  (authenticated automatically)
 *
 * Installs the verified UI archive and restarts the web server.
 * Unlike install_fw, this does NOT reboot the device.
 */
hem_error_t hem_upgrade_install_ui(hem_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* HEM_UPGRADE_H */
