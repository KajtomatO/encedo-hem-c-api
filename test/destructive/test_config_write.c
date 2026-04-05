/*
 * Destructive test: POST /api/system/config
 * Changes the device user name and restores it.
 *
 * WARNING: Modifies persistent device configuration.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_MASTER_PASS=secret ./test_config_write
 */
#include <stdio.h>
#include <time.h>
#include "../test_helpers.h"
#include "hem/hem_system.h"

/* -------------------------------------------------------------------------
 * Change user name to a timestamped value, verify, then restore.
 * ---------------------------------------------------------------------- */
static void test_config_set_user(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* Read current user name */
    hem_config_t orig = {0};
    assert_hem_ok(ctx, hem_system_config(ctx, &orig));
    print_message("  original user: '%s'\n", orig.user);

    /* Set a timestamped test name */
    char test_name[64];
    snprintf(test_name, sizeof(test_name), "test-user-%ld", (long)time(NULL));

    hem_config_update_t result = {0};
    assert_hem_ok(ctx, hem_system_config_set(ctx, test_name, NULL, &result));
    assert_true(result.updated);
    print_message("  set user to '%s', reboot_required=%d\n",
                  test_name, (int)result.reboot_required);

    /* Verify */
    hem_config_t current = {0};
    assert_hem_ok(ctx, hem_system_config(ctx, &current));
    assert_string_equal(current.user, test_name);

    /* Restore */
    if (orig.user[0]) {
        assert_hem_ok(ctx, hem_system_config_set(ctx, orig.user, NULL, NULL));
        print_message("  restored user to '%s'\n", orig.user);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_config_set_user, setup_master, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
