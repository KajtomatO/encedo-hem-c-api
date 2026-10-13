/*
 * test_backend_compat.c — the crypto-backend compatibility check that
 * ehem_global_init() runs (src/crypto_shim.c), driven through the test seam.
 *
 * verifies: REQ-API-010 (the matching library passes the version check and the
 *           six guard-banded probes; a forced version mismatch and a forced
 *           probe failure each yield EHEM_ERR_BACKEND_MISMATCH; the implicit
 *           init paths — ehem_ctx_create standing up the default transport,
 *           the login flow's first acquisition over a caller-supplied
 *           transport — carry that code out instead of EHEM_ERR_NOMEM, before
 *           any traffic; a refused init
 *           leaves ehem_global_cleanup() a no-op and a later init succeeds),
 *           REQ-API-002 (the init/cleanup idempotency criterion extended across
 *           a refusal),
 *           REQ-API-003 (the appended value is named and never renumbered)
 *
 * The whole unit suite is the "matching library" proof — every test process
 * initialises against the wolfSSL the build linked; this file adds the forced
 * refusals, which no real library can produce on the CI hosts.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "crypto_shim.h"      /* internal: ehem_crypto_test_force_mismatch */
#include "proto_auth.h"       /* internal: ehem_auth_ensure_token (the login flow) */
#include "fake_transport.h"

/* Every test starts from "not initialised": the public wrapper is idempotent,
 * so a ready global state would never reach the check being tested. */
static int setup_not_ready(void **state)
{
    (void)state;
    ehem_global_cleanup();
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_NONE);
    return 0;
}

static int teardown_restore(void **state)
{
    (void)state;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_NONE);
    ehem_global_cleanup();
    return 0;
}

/* The real library: exact version + Hmac, WC_RNG, DecodedCert, Aes,
 * ed25519_key and ed448_key probes all pass. */
static void test_matching_library_passes(void **state)
{
    (void)state;
    assert_int_equal(ehem_global_init(), EHEM_OK);
    ehem_global_cleanup();
}

/* Version mismatch: refused before wolfCrypt_Init — and refused AGAIN on the
 * next call (the verdict is re-derived, not cached as "ready"). */
static void test_forced_version_mismatch(void **state)
{
    (void)state;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
}

/* Probe failure: the version matches, wolfCrypt_Init runs, the first probe's
 * known answer is wrong → refused through the comparison path. */
static void test_forced_probe_failure(void **state)
{
    (void)state;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_PROBE);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
}

/* REQ-API-002 extended: after either refusal, cleanup is a no-op and a later
 * init (seam cleared) succeeds; the pair stays idempotent throughout. */
static void test_refusal_leaves_global_state_untouched(void **state)
{
    (void)state;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    ehem_global_cleanup();                  /* nothing was readied: no-op */

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_PROBE);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    ehem_global_cleanup();                  /* likewise */

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_NONE);
    assert_int_equal(ehem_global_init(), EHEM_OK);      /* re-checks → passes */
    assert_int_equal(ehem_global_init(), EHEM_OK);      /* idempotent */
    ehem_global_cleanup();
    ehem_global_cleanup();                  /* second cleanup: no-op, no crash */
    assert_int_equal(ehem_global_init(), EHEM_OK);      /* re-init after cleanup */
    ehem_global_cleanup();
}

/* Implicit init #1: ehem_ctx_create() standing up the default transport
 * reports the refusal itself (1.1.0 returned EHEM_ERR_NOMEM here). */
static void test_ctx_create_default_transport_reports_mismatch(void **state)
{
    (void)state;
    ehem_ctx *ctx = (ehem_ctx *)0x1;      /* must be reset to NULL on failure */

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx),
                     EHEM_ERR_BACKEND_MISMATCH);
    assert_null(ctx);

    ctx = (ehem_ctx *)0x1;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_PROBE);
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx),
                     EHEM_ERR_BACKEND_MISMATCH);
    assert_null(ctx);

    /* Seam cleared: the same call stands the default transport up. */
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_NONE);
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);
    assert_non_null(ctx);
    ehem_ctx_destroy(ctx);
}

/* Implicit init #2: a caller-supplied transport skips the default factory, so
 * creation does not initialise. ehem_login() only stores the credential; the
 * login flow self-initialises at the first token acquisition (the challenge /
 * derive / POST sequence) — that is where the refusal must surface, before
 * any request leaves. */
static void test_login_caller_transport_reports_mismatch(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    const char *tok = NULL;
    assert_non_null(fake);

    ehem_options_init(&opts);
    opts.transport = fake;
    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, "secret"), EHEM_OK);

    assert_int_equal(ehem_auth_ensure_token(ctx, "keymgmt:list", &tok),
                     EHEM_ERR_BACKEND_MISMATCH);
    assert_null(tok);
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_non_null(strstr(ehem_last_error(ctx)->message,
                           "process-global init failed"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* REQ-API-003: appended after EHEM_ERR_UNSUPPORTED, named by ehem_rc_str(). */
static void test_rc_value_appended_and_named(void **state)
{
    (void)state;
    assert_int_equal((int)EHEM_ERR_BACKEND_MISMATCH, (int)EHEM_ERR_UNSUPPORTED + 1);
    assert_string_equal(ehem_rc_str(EHEM_ERR_BACKEND_MISMATCH),
                        "EHEM_ERR_BACKEND_MISMATCH");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_matching_library_passes,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_forced_version_mismatch,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_forced_probe_failure,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_refusal_leaves_global_state_untouched,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_ctx_create_default_transport_reports_mismatch,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_login_caller_transport_reports_mismatch,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test(test_rc_value_appended_and_named),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
