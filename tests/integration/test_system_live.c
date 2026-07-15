/*
 * test_system_live.c — first integration test: a status + version round-trip
 * against the real dev-machine HEM.
 *
 * verifies: REQ-TEST-002 (gating), and exercises REQ-SYS-001 / REQ-SYS-002
 *           against a live device (the M1 "hello device" round-trip).
 *
 * Skipped (exit 77 → CTest "Skipped") unless EHEM_TEST_URL is set. Uses only
 * the public API through the shared library. See tests/README.md to run.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "integration_env.h"

/* GET status then version from the live device; both must succeed and carry
 * the fields the M1 gate needs (uptime; hardware/firmware/bootloader). */
static void test_status_version_roundtrip(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_version_info *ver = NULL;
    ehem_rc rc;

    rc = ehem_test_ctx(&ctx);
    assert_int_equal(rc, EHEM_OK);
    assert_non_null(ctx);

    rc = ehem_system_status(ctx, &st);
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_status failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(st);
    assert_true(st->uptime >= 0);
    ehem_system_status_free(st);

    rc = ehem_system_version(ctx, &ver);
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_version failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(ver);
    assert_non_null(ver->hwv);
    assert_non_null(ver->fwv);
    assert_non_null(ver->blv);
    ehem_system_version_free(ver);

    ehem_ctx_destroy(ctx);
}

int main(void)
{
    /* No device configured → skip (reported skipped, not failed). */
    ehem_require_test_url();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_status_version_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
