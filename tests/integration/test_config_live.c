/*
 * test_config_live.c — live authenticated GET /api/system/config against the
 * real dev-machine HEM.
 *
 * verifies: REQ-SYS-004 (the config binding end-to-end: login + a scoped
 *           system:config call returns the typed struct with the known
 *           hostname) — the M2-gate open criterion. Unlike test_auth_live this
 *           uses ONLY the public API (ehem_login + ehem_system_config), so it
 *           links the shared library like the other integration tests.
 *           REQ-SYS-007 (live selftest: fls_state==0, repo_stats present,
 *           latency + any-scope facts recorded), REQ-SYS-011 (live
 *           attestation: crt-vs-csr shape, genuine non-empty, PPA/EPA
 *           routing recorded). REQ-SYS-008's shutdown live leg is
 *           deliberately ABSENT: recovery needs a physical power-cycle
 *           (attended-manual only, never automated).
 *
 * Skipped (exit 77 → CTest "Skipped") unless EHEM_TEST_URL and
 * EHEM_TEST_PASSPHRASE are set (REQ-TEST-002).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

/* Live selftest: the battery runs, the device reports nominal, repo stats are
 * visible; wall-clock latency recorded for the REQ-SYS-007 criterion. */
static void test_selftest_live(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_selftest_info *info = NULL;
    time_t t0, t1;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    t0 = time(NULL);
    rc = ehem_system_selftest(ctx, &info);
    t1 = time(NULL);
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_selftest failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    printf("[test_config_live] selftest: fls_state=%lld kat_busy=%d "
           "se_state=%lld repo{total=%lld deleted=%lld invalid=%lld "
           "fragmented=%lld freeslots=%lld} latency≈%lds\n",
           (long long)info->fls_state, (int)info->kat_busy,
           (long long)info->se_state, (long long)info->repo_total,
           (long long)info->repo_deleted, (long long)info->repo_invalid,
           (long long)info->repo_fragmented, (long long)info->repo_freeslots,
           (long)(t1 - t0));
    assert_int_equal((int)info->fls_state, 0);       /* device says healthy */
    assert_true(info->selftest_ts > 0);
    assert_true(info->repo_total >= 0);              /* stats block present */

    ehem_selftest_free(info);
    ehem_logout(ctx);
    ehem_ctx_destroy(ctx);
}

/* Live attestation: provisioned device → crt shape; leaf inspected via the
 * REQ-SYS-006 public inspector; PPA/EPA routing recorded. */
static void test_attestation_live(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_attestation_info *a = NULL;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_system_attestation(ctx, &a);
    if (rc == EHEM_ERR_NOT_FOUND) {
        /* EPA build: the route is absent — recorded, not a failure. */
        printf("[test_config_live] attestation: 404 — EPA build (no ATECC "
               "route); recorded for REQ-SYS-011\n");
        ehem_logout(ctx);
        ehem_ctx_destroy(ctx);
        skip();
        return;
    }
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_attestation failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(a->genuine);
    assert_true(a->genuine[0] != '\0');
    if (a->crt_b64 != NULL) {
        ehem_cert_info *leaf = NULL;
        rc = ehem_cert_inspect(ctx, a->crt_b64, &leaf);
        if (rc == EHEM_OK) {
            printf("[test_config_live] attestation crt leaf: serial=%s "
                   "subject=%s\n", leaf->serial ? leaf->serial : "?",
                   leaf->subject_cn ? leaf->subject_cn : "?");
            ehem_cert_info_free(leaf);
        } else {
            printf("[test_config_live] attestation crt: inspect rc=%s "
                   "(recorded)\n", ehem_rc_str(rc));
        }
        printf("[test_config_live] attestation: provisioned shape (crt)\n");
    } else {
        printf("[test_config_live] attestation: fresh-chip shape (csr=%s)\n",
               a->csr_pem != NULL ? "present" : "MISSING");
        assert_non_null(a->csr_pem);
    }

    ehem_attestation_free(a);
    ehem_logout(ctx);
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
        cmocka_unit_test(test_selftest_live),
        cmocka_unit_test(test_attestation_live),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
