/*
 * Destructive test: GET /api/storage/unlock and /lock  (PPA only)
 *
 * WARNING: Changes disk lock state.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_MASTER_PASS=secret ./test_storage
 */
#include "../test_helpers.h"
#include "hem/hem_storage.h"

/* -------------------------------------------------------------------------
 * Unlock disk0 read-write, then lock it again.
 * ---------------------------------------------------------------------- */
static void test_unlock_lock_disk0_rw(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_error_t err = hem_storage_unlock(ctx, "storage:disk0:rw");

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  storage: device is EPA, skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);
    print_message("  disk0 unlocked (rw)\n");

    assert_hem_ok(ctx, hem_storage_lock(ctx, "storage:disk0:rw"));
    print_message("  disk0 locked\n");
}

/* -------------------------------------------------------------------------
 * Unlock disk0 read-only.
 * ---------------------------------------------------------------------- */
static void test_unlock_disk0_ro(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    hem_error_t err = hem_storage_unlock(ctx, "storage:disk0:ro");

    if (err == HEM_ERR_HTTP_STATUS && hem_last_http_status(ctx) == 404) {
        print_message("  storage: device is EPA, skipping\n");
        return;
    }

    assert_hem_ok(ctx, err);
    print_message("  disk0 unlocked (ro)\n");

    /* Lock requires rw scope per spec */
    assert_hem_ok(ctx, hem_storage_lock(ctx, "storage:disk0:rw"));
    print_message("  disk0 locked\n");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_unlock_lock_disk0_rw, setup_master, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_unlock_disk0_ro,       setup_master, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
