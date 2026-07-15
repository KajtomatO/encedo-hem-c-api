/*
 * test_context.c — ehem_ctx lifecycle, options, error enum, last-error,
 * global init/cleanup.
 *
 * verifies: REQ-API-001, REQ-API-002, REQ-API-003, REQ-API-004
 *
 * Reaches the internal error-setting helpers (context.h) to exercise
 * ehem_last_error without a transport — the fake transport lands in
 * STEP-M1-050, but the last-error contract is testable now by simulating a
 * failure directly on the context.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "context.h"   /* internal: ehem_ctx_fail / ehem_ctx_clear_error */

/* --- lifecycle + URL validation (REQ-API-001) ----------------------------- */

static void test_create_destroy_defaults(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);
    assert_non_null(ctx);
    ehem_ctx_destroy(ctx);
}

static void test_create_rejects_bad_args(void **state)
{
    (void)state;
    ehem_ctx *ctx = (ehem_ctx *)0x1;  /* must be overwritten to NULL on failure */

    assert_int_equal(ehem_ctx_create(NULL, NULL, &ctx), EHEM_ERR_ARG);
    assert_null(ctx);

    ctx = (ehem_ctx *)0x1;
    assert_int_equal(ehem_ctx_create("ftp://nope", NULL, &ctx), EHEM_ERR_ARG);
    assert_null(ctx);

    ctx = (ehem_ctx *)0x1;
    assert_int_equal(ehem_ctx_create("https://", NULL, &ctx), EHEM_ERR_ARG);
    assert_null(ctx);

    /* out == NULL is itself an argument error and must not crash. */
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, NULL), EHEM_ERR_ARG);
}

static void test_destroy_null_is_safe(void **state)
{
    (void)state;
    ehem_ctx_destroy(NULL);   /* no-op, no crash */
}

/* Two contexts for different URLs coexist independently (REQ-API-001/002). */
static void test_two_contexts_independent(void **state)
{
    (void)state;
    ehem_ctx *a = NULL, *b = NULL;
    assert_int_equal(ehem_ctx_create("https://one.local", NULL, &a), EHEM_OK);
    assert_int_equal(ehem_ctx_create("http://two.local:8080", NULL, &b), EHEM_OK);
    assert_non_null(a);
    assert_non_null(b);
    assert_ptr_not_equal(a, b);

    /* A failure recorded on one context must not appear on the other. */
    ehem_ctx_fail(a, EHEM_ERR_DEVICE, 500, "boom", "device said %d", 500);
    assert_int_equal(ehem_last_error(a)->http_status, 500);
    assert_int_equal(ehem_last_error(b)->http_status, 0);
    assert_null(ehem_last_error(b)->device_payload);

    ehem_ctx_destroy(a);
    ehem_ctx_destroy(b);
}

/* --- options (REQ-API-001, REQ-NET-003) ----------------------------------- */

static void test_options_init_and_apply(void **state)
{
    (void)state;
    ehem_options opts;
    ehem_options_init(&opts);
    assert_int_equal((int)opts.abi_size, (int)sizeof(opts));
    assert_int_equal(opts.connect_timeout_ms, EHEM_DEFAULT_CONNECT_TIMEOUT_MS);
    assert_int_equal(opts.total_timeout_ms, EHEM_DEFAULT_TOTAL_TIMEOUT_MS);
    assert_int_equal(opts.tls_mode, EHEM_TLS_SYSTEM);

    opts.connect_timeout_ms = 1234;
    opts.tls_mode = EHEM_TLS_INSECURE;
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    assert_non_null(ctx);
    ehem_ctx_destroy(ctx);
}

/* CA_FILE trust mode without a certificate path is an argument error. */
static void test_options_ca_file_requires_path(void **state)
{
    (void)state;
    ehem_options opts;
    ehem_options_init(&opts);
    opts.tls_mode = EHEM_TLS_CA_FILE;   /* but ca_file left NULL */
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_ERR_ARG);
    assert_null(ctx);

    opts.ca_file = "/etc/ssl/hem-ca.pem";
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    assert_non_null(ctx);
    ehem_ctx_destroy(ctx);
}

/* A non-NULL options block that was never initialized (abi_size 0) is rejected. */
static void test_options_uninitialized_rejected(void **state)
{
    (void)state;
    ehem_options opts;
    memset(&opts, 0, sizeof opts);   /* abi_size == 0: caller forgot _init */
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_ERR_ARG);
    assert_null(ctx);
}

/* --- error enum (REQ-API-003) --------------------------------------------- */

static void test_rc_str_covers_every_value(void **state)
{
    (void)state;
    /* Sentinel string for an out-of-range value; every real value must differ. */
    const char *unknown = ehem_rc_str((ehem_rc)(EHEM_ERR_UNSUPPORTED + 1));
    assert_non_null(unknown);

    for (int i = EHEM_OK; i <= EHEM_ERR_UNSUPPORTED; i++) {
        const char *s = ehem_rc_str((ehem_rc)i);
        assert_non_null(s);
        assert_true(s[0] != '\0');
        assert_string_not_equal(s, unknown);   /* dedicated, not the fallback */
    }
    assert_string_equal(ehem_rc_str(EHEM_OK), "EHEM_OK");
}

/* --- last-error detail (REQ-API-004) -------------------------------------- */

static void test_last_error_set_and_reset(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);

    /* Fresh context: empty/success detail. */
    const ehem_error *e = ehem_last_error(ctx);
    assert_non_null(e);
    assert_int_equal(e->http_status, 0);
    assert_null(e->device_payload);
    assert_non_null(e->message);          /* never NULL */
    assert_string_equal(e->message, "");

    /* Simulate a failing call (fake transport arrives in STEP-M1-050). */
    ehem_rc rc = ehem_ctx_fail(ctx, EHEM_ERR_DEVICE, 409,
                               "{\"error\":\"conflict\"}",
                               "device rejected request (%d)", 409);
    assert_int_equal(rc, EHEM_ERR_DEVICE);
    e = ehem_last_error(ctx);
    assert_int_equal(e->http_status, 409);
    assert_non_null(e->device_payload);
    assert_string_equal(e->device_payload, "{\"error\":\"conflict\"}");
    assert_string_equal(e->message, "device rejected request (409)");

    /* A successful call resets the detail. */
    ehem_ctx_clear_error(ctx);
    e = ehem_last_error(ctx);
    assert_int_equal(e->http_status, 0);
    assert_null(e->device_payload);
    assert_string_equal(e->message, "");

    ehem_ctx_destroy(ctx);
}

static void test_last_error_null_ctx(void **state)
{
    (void)state;
    assert_null(ehem_last_error(NULL));
}

/* --- global init / cleanup idempotency (REQ-API-002) ---------------------- */

static void test_global_init_cleanup_idempotent(void **state)
{
    (void)state;
    /* Double init, double cleanup, and init-after-cleanup are all safe. */
    assert_int_equal(ehem_global_init(), EHEM_OK);
    assert_int_equal(ehem_global_init(), EHEM_OK);
    ehem_global_cleanup();
    ehem_global_cleanup();                 /* second cleanup: no-op, no crash */
    assert_int_equal(ehem_global_init(), EHEM_OK);  /* re-init after cleanup */
    ehem_global_cleanup();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_destroy_defaults),
        cmocka_unit_test(test_create_rejects_bad_args),
        cmocka_unit_test(test_destroy_null_is_safe),
        cmocka_unit_test(test_two_contexts_independent),
        cmocka_unit_test(test_options_init_and_apply),
        cmocka_unit_test(test_options_ca_file_requires_path),
        cmocka_unit_test(test_options_uninitialized_rejected),
        cmocka_unit_test(test_rc_str_covers_every_value),
        cmocka_unit_test(test_last_error_set_and_reset),
        cmocka_unit_test(test_last_error_null_ctx),
        cmocka_unit_test(test_global_init_cleanup_idempotent),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
