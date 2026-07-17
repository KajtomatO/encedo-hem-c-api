/*
 * test_storage_live.c — ATTENDED live probe of storage unlock/lock.
 *
 * verifies: REQ-SYS-010 (live: unlock disk 0 read-only → lock disk 0; the
 *           device stays responsive — resolving the uninitialized-`sub`
 *           firmware bug's practical behavior and the python OQ-24 "no-op"
 *           observation; rw-scope variant probed)
 *
 * DISRUPTIVE-gated ON PURPOSE for the first runs (REQ-SYS-010): both
 * v1.2.2 storage handlers read an UNINITIALIZED pointer in their scope
 * check — a hard-fault would self-reset the device mid-call. Run attended.
 * Once proven stable, the REQ's open criterion decides whether this moves
 * to the plain `integration` label. Side effect while unlocked: the microSD
 * partition becomes visible to whatever host the device's USB is plugged
 * into (locked again before exit).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/storage.h"
#include "ehem/system.h"
#include "integration_env.h"

static void test_storage_unlock_lock_ro(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_storage_unlock(ctx, 0, 0);
    printf("[storage probe] unlock disk0 ro: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(ctx)->http_status);
    if (rc == EHEM_ERR_NOT_FOUND) {
        printf("[storage probe] route absent (EPA?) — recorded\n");
        ehem_logout(ctx);
        ehem_ctx_destroy(ctx);
        skip();
        return;
    }
    assert_int_equal(rc, EHEM_OK);

    /* The device survived the uninitialized-`sub` scope check — confirm it
     * is still serving requests, then withdraw the partition. */
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_OK);
    ehem_system_status_free(st);

    rc = ehem_storage_lock(ctx, 0);
    printf("[storage probe] lock disk0: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(ctx)->http_status);
    assert_int_equal(rc, EHEM_OK);

    ehem_logout(ctx);
    ehem_ctx_destroy(ctx);
}

static void test_storage_rw_scope_variant(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    /* The :rw scope form end-to-end (grantable because the eJWT self-selects
     * its scope on this auth model), then immediately re-lock. */
    rc = ehem_storage_unlock(ctx, 0, 1);
    printf("[storage probe] unlock disk0 RW: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(ctx)->http_status);
    if (rc == EHEM_OK) {
        assert_int_equal(ehem_storage_lock(ctx, 0), EHEM_OK);
    }
    /* No hard assert beyond re-lock: the rw grant policy is the probe's
     * finding, recorded in REQ-SYS-010 either way. */

    ehem_logout(ctx);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_disruptive();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_storage_unlock_lock_ro),
        cmocka_unit_test(test_storage_rw_scope_variant),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
