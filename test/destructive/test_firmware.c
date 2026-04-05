/*
 * Destructive test: firmware upgrade endpoints.
 *
 * WARNING: install_fw overwrites firmware and reboots the device.
 *          Only run on a dedicated test device with a known-good image.
 *
 * NOTE: upload_fw and upload_ui are NOT implemented pending OQ-9 resolution
 *       (binary upload format -- multipart vs raw octet-stream -- undefined).
 *       Tests for check_fw and install_fw therefore require a firmware image
 *       to have been uploaded by other means first.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_PASS=secret ./test_firmware
 */
#include "../test_helpers.h"
#include "hem/hem_upgrade.h"

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/usbmode  (PPA only)
 * ---------------------------------------------------------------------- */
static void test_usbmode(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_error_t err = hem_upgrade_usbmode(ctx);

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  usbmode: device is EPA, skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);
    print_message("  device entered USB DFU mode\n");
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/check_fw
 * Requires a firmware image to have been uploaded first (via upload_fw,
 * which is pending OQ-9).  Skips gracefully if no image is present.
 * ---------------------------------------------------------------------- */
static void test_check_fw(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_error_t err = hem_upgrade_check_fw(ctx);

    if (err == HEM_ERR_HTTP_STATUS) {
        print_message("  check_fw: HTTP %d -- no firmware uploaded or not applicable\n",
                      hem_last_http_status(ctx));
        return;  /* acceptable without a prior upload */
    }

    assert_hem_ok(ctx, err);
    print_message("  firmware image verified\n");
}

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/install_fw
 * Only run this if check_fw passed and you intend to replace the firmware.
 * Commented out by default -- uncomment deliberately.
 * ---------------------------------------------------------------------- */
#if 0
static void test_install_fw(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    print_message("  installing firmware -- device will reboot\n");
    assert_hem_ok(ctx, hem_upgrade_install_fw(ctx));
    print_message("  install_fw acknowledged, device is rebooting\n");
}
#endif

/* -------------------------------------------------------------------------
 * GET /api/system/upgrade/check_ui + install_ui
 * ---------------------------------------------------------------------- */
static void test_check_ui(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_error_t err = hem_upgrade_check_ui(ctx);

    if (err == HEM_ERR_HTTP_STATUS) {
        print_message("  check_ui: HTTP %d -- no UI archive uploaded or not applicable\n",
                      hem_last_http_status(ctx));
        return;
    }

    assert_hem_ok(ctx, err);
    print_message("  UI archive verified\n");

    /* install_ui does not reboot -- safe to chain immediately after check */
    assert_hem_ok(ctx, hem_upgrade_install_ui(ctx));
    print_message("  UI installed\n");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_usbmode,  setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_check_fw, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_check_ui, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
