/*
 * test_config_live.c — live authenticated GET /api/system/config against the
 * real dev-machine HEM.
 *
 * verifies: REQ-SYS-004 (the config binding end-to-end: login + a scoped
 *           system:config call returns the typed struct with the known
 *           hostname) — the M2-gate open criterion. Unlike test_auth_live this
 *           uses ONLY the public API (ehem_login + ehem_system_config), so it
 *           links the shared library like the other integration tests.
 *
 * Skipped (exit 77 → CTest "Skipped") unless EHEM_TEST_URL and
 * EHEM_TEST_PASSPHRASE are set (REQ-TEST-002).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/system.h"
#include "integration_env.h"

static void test_config_hostname(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_config_info *cfg = NULL;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_non_null(ctx);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_system_config(ctx, &cfg);
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_config failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(cfg);
    assert_non_null(cfg->devid);
    assert_non_null(cfg->user);
    assert_non_null(cfg->hostname);
    assert_string_equal(cfg->hostname, "my.ence.do");

    printf("[test_config_live] devid=%s hostname=%s user=%s — config GET OK\n",
           cfg->devid, cfg->hostname, cfg->user);

    ehem_system_config_free(cfg);
    assert_int_equal(ehem_logout(ctx), EHEM_OK);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live config test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_hostname),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
