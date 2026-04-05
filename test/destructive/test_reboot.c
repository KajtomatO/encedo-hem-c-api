/*
 * Destructive test: GET /api/system/reboot
 * Reboots the device and verifies it comes back online.
 *
 * WARNING: Device goes offline for ~30 seconds. All active sessions are lost.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_PASS=secret ./test_reboot
 */
#include <unistd.h>
#include "../test_helpers.h"
#include "hem/hem_system.h"

#define REBOOT_WAIT_S      30   /* initial wait after reboot */
#define REBOOT_POLL_S       2   /* poll interval */
#define REBOOT_MAX_POLLS   30   /* max 60 s recovery wait */

static void test_reboot_and_recover(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    print_message("  rebooting device...\n");
    assert_hem_ok(ctx, hem_system_reboot(ctx));
    print_message("  reboot acknowledged, waiting %ds...\n", REBOOT_WAIT_S);

    sleep(REBOOT_WAIT_S);

    /* Poll until device responds */
    hem_status_t s = {0};
    int recovered = 0;
    for (int i = 0; i < REBOOT_MAX_POLLS; i++) {
        hem_error_t err = hem_system_status(ctx, &s);
        if (err == HEM_OK) {
            recovered = 1;
            break;
        }
        print_message("  still waiting (%d/%d)...\n", i + 1, REBOOT_MAX_POLLS);
        sleep(REBOOT_POLL_S);
    }

    assert_true(recovered);
    assert_int_equal(s.fls_state, 0);
    assert_true(s.initialized);
    print_message("  device recovered: fls_state=%d, uptime=%ds\n",
                  s.fls_state, s.uptime);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_reboot_and_recover, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
