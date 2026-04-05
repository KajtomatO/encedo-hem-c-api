/*
 * Integration tests for read-only system endpoints.
 * Safe to run on any device -- no state is changed.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_PASS=secret ./test_system
 * Note: test_selftest may take up to 4 minutes.
 */
#include "../test_helpers.h"
#include "hem/hem_system.h"

/* -------------------------------------------------------------------------
 * GET /api/system/version
 * ---------------------------------------------------------------------- */
static void test_version(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_version_t v = {0};
    assert_hem_ok(ctx, hem_system_version(ctx, &v));
    assert_true(v.fwv[0] != '\0');
}

/* -------------------------------------------------------------------------
 * GET /api/system/status
 * ---------------------------------------------------------------------- */
static void test_status(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_status_t s = {0};
    assert_hem_ok(ctx, hem_system_status(ctx, &s));
    assert_int_equal(s.fls_state, 0);
    assert_true(s.initialized);
}

/* -------------------------------------------------------------------------
 * GET /api/system/checkin
 * ---------------------------------------------------------------------- */
static void test_checkin(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;
    assert_hem_ok(ctx, hem_system_checkin(ctx));
}

/* -------------------------------------------------------------------------
 * GET /api/system/config
 * ---------------------------------------------------------------------- */
static void test_config_get(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_config_t cfg = {0};
    assert_hem_ok(ctx, hem_system_config(ctx, &cfg));
    assert_true(cfg.eid[0] != '\0');
}

/* -------------------------------------------------------------------------
 * GET /api/system/selftest  (slow -- up to 4 minutes)
 * ---------------------------------------------------------------------- */
static void test_selftest(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    int fls = -1;
    assert_hem_ok(ctx, hem_system_selftest(ctx, &fls));
    assert_int_equal(fls, 0);
}

/* -------------------------------------------------------------------------
 * GET /api/system/config/attestation  (PPA only -- 404 on EPA is accepted)
 * ---------------------------------------------------------------------- */
static void test_attestation(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char genuine[4096] = {0};
    hem_error_t err = hem_system_attestation(ctx, genuine, sizeof(genuine));

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  attestation: device is EPA, skipping\n");
        return;  /* expected on EPA */
    }

    assert_hem_ok(ctx, err);
    assert_true(genuine[0] != '\0');
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_version,     setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_status,      setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_checkin,     setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_config_get,  setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_selftest,    setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_attestation, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
