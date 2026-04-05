/*
 * Destructive test: GET /api/system/shutdown  (PPA only)
 *
 * WARNING: Powers off the device. Physical intervention required to restart.
 *          This test does NOT verify the device is actually off -- that
 *          requires manual observation.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_PASS=secret ./test_shutdown
 */
#include "../test_helpers.h"
#include "hem/hem_system.h"

static void test_shutdown(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    print_message("  sending shutdown command...\n");

    hem_error_t err = hem_system_shutdown(ctx);

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  shutdown: device is EPA (not supported), skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);
    print_message("  shutdown acknowledged. Device is now powering off.\n");
    print_message("  Manual intervention required to restart the device.\n");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_shutdown, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
