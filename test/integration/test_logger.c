/*
 * Integration tests for audit log endpoints (PPA only).
 * Logger list and download return 404 on EPA -- this is handled gracefully.
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_logger.h"

/* -------------------------------------------------------------------------
 * GET /api/logger/key
 * ---------------------------------------------------------------------- */
static void test_logger_key(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_logger_key_t k = {0};
    assert_hem_ok(ctx, hem_logger_key(ctx, &k));
    assert_true(k.key_b64[0] != '\0');
    assert_true(k.nonce_b64[0] != '\0');
    assert_true(k.nonce_signed_b64[0] != '\0');
}

/* -------------------------------------------------------------------------
 * GET /api/logger/list/{offset}
 * ---------------------------------------------------------------------- */
static void test_logger_list(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    int ids[32] = {0};
    int count   = 0;
    hem_error_t err = hem_logger_list(ctx, 0, ids, 32, &count);

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  logger list: device is EPA, skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);
    assert_true(count >= 0);
    print_message("  logger list: %d log file(s)\n", count);
}

/* -------------------------------------------------------------------------
 * GET /api/logger/{id}
 * ---------------------------------------------------------------------- */
static void test_logger_download(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* First get the list */
    int ids[32] = {0};
    int count   = 0;
    hem_error_t err = hem_logger_list(ctx, 0, ids, 32, &count);

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  logger download: device is EPA, skipping\n");
        return;
    }
    assert_hem_ok(ctx, err);

    if (count == 0) {
        print_message("  logger download: no log files present, skipping\n");
        return;
    }

    /* Download first log file */
    char buf[65536] = {0};
    size_t out_len = 0;
    assert_hem_ok(ctx, hem_logger_download(ctx, ids[0], buf, sizeof(buf), &out_len));
    assert_true(out_len > 0);

    /* Spot-check: response should contain pipe characters (field delimiter) */
    /* See OQ-8: field names not documented, just verify format exists */
    assert_non_null(strchr(buf, '|'));
    print_message("  downloaded log %d: %zu bytes\n", ids[0], out_len);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_logger_key,      setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_logger_list,     setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_logger_download, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
