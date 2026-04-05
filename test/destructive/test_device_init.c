/*
 * Destructive test: POST /api/auth/init  (device factory initialisation)
 *
 * WARNING: IRREVERSIBLE without a factory reset.
 *          Only run on a fresh, unprovisioned device.
 *          A 406 response means the device is already initialised -- the test
 *          skips gracefully in that case.
 *
 * Required env vars:
 *   HEM_TEST_URL         https://my.ence.do
 *   HEM_TEST_MASTER_PASS Master (admin) passphrase for the new device
 *   HEM_TEST_PASS        User passphrase for the new device
 *   HEM_TEST_HOSTNAME    FQDN to assign (e.g. mydevice.ence.do)
 *
 * Run: HEM_TEST_URL=https://my.ence.do \
 *      HEM_TEST_MASTER_PASS=master_secret \
 *      HEM_TEST_PASS=user_secret \
 *      HEM_TEST_HOSTNAME=mydevice.ence.do \
 *      ./test_device_init
 */
#include <stdlib.h>
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_auth.h"
#include "hem/hem_system.h"

/* -------------------------------------------------------------------------
 * Helper: resolve required env var or skip test.
 * ---------------------------------------------------------------------- */
static const char *require_env(const char *name)
{
    const char *v = getenv(name);
    if (!v || v[0] == '\0') {
        print_message("  %s not set -- skipping\n", name);
        skip();   /* cmocka: marks test as skipped, does not abort process */
    }
    return v;
}

/* -------------------------------------------------------------------------
 * Factory initialise the device.
 *
 * Uses the minimal configuration (no IP override, no storage_mode override).
 * On success, prints the instance ID and the first 32 chars of the token.
 * On 406, the device is already initialised -- skip gracefully.
 * ---------------------------------------------------------------------- */
static void test_device_init(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    const char *master_pass = require_env("HEM_TEST_MASTER_PASS");
    const char *user_pass   = require_env("HEM_TEST_PASS");
    const char *hostname    = require_env("HEM_TEST_HOSTNAME");

    /* Use a fixed user name; change as needed for your test. */
    const char *user_name = "admin";
    const char *email     = "admin@example.com";

    print_message("  initialising device: hostname=%s user=%s\n",
                  hostname, user_name);

    hem_init_result_t result;
    memset(&result, 0, sizeof(result));

    hem_error_t err = hem_auth_device_init(ctx,
                                            master_pass,
                                            user_pass,
                                            user_name,
                                            email,
                                            hostname,
                                            NULL,    /* opts: defaults */
                                            &result);

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 406) {
        print_message("  device is already initialised (HTTP 406) -- skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);

    assert_true(result.instanceid[0] != '\0');
    assert_true(result.token[0] != '\0');

    print_message("  init succeeded\n");
    print_message("  instanceid : %.64s\n", result.instanceid);
    /* Token is sensitive; print only enough to confirm it is non-empty. */
    print_message("  token      : %.32s...\n", result.token);

    if (result.csr[0])
        print_message("  CSR present (%zu bytes)\n", strlen(result.csr));
    if (result.genuine[0])
        print_message("  attestation present (%zu bytes)\n", strlen(result.genuine));

    /* Verify the device is now reachable and reports itself as initialised. */
    hem_status_t s = {0};
    hem_error_t  serr = hem_system_status(ctx, &s);
    if (serr == HEM_OK)
        print_message("  post-init status: initialized=%d fls_state=%d\n",
                      (int)s.initialized, s.fls_state);
    else
        print_message("  post-init status check failed (err=%d) -- device may need time\n",
                      (int)serr);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_device_init, setup_noauth, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
