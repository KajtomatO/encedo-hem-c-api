/*
 * test_cert.c — the public certificate-inspection helper ehem_cert_inspect(),
 * driven entirely offline against the frozen leaf fixture.
 *
 * verifies: REQ-SYS-006 / REQ-TOOL-003 (read the leaf serial + validity window
 *           the skip-if-current comparison and the cert-install summary rely on)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "fixtures/cert_fixture.h"

static ehem_ctx *make_ctx(void)
{
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);
    return ctx;
}

/* The leaf's serial, CN and validity window are read exactly. */
static void test_inspect_leaf_fields(void **state)
{
    (void)state;
    ehem_ctx *ctx = make_ctx();
    ehem_cert_info *info = NULL;

    assert_int_equal(ehem_cert_inspect(ctx, EHEM_FX_LEAF_B64, &info), EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->serial, EHEM_FX_LEAF_SERIAL);
    assert_string_equal(info->subject_cn, EHEM_FX_LEAF_CN);
    assert_string_equal(info->not_before, EHEM_FX_LEAF_NOT_BEFORE);
    assert_string_equal(info->not_after, EHEM_FX_LEAF_NOT_AFTER);

    ehem_cert_info_free(info);
    ehem_ctx_destroy(ctx);
}

/* Not valid base64 → EHEM_ERR_PROTOCOL, *out left NULL, detail recorded. */
static void test_inspect_bad_base64(void **state)
{
    (void)state;
    ehem_ctx *ctx = make_ctx();
    ehem_cert_info *info = NULL;

    assert_int_equal(ehem_cert_inspect(ctx, "!!!!not base64!!!!", &info),
                     EHEM_ERR_PROTOCOL);
    assert_null(info);
    assert_non_null(ehem_last_error(ctx)->message);

    ehem_ctx_destroy(ctx);
}

/* Valid base64 that is not an X.509 certificate → EHEM_ERR_PROTOCOL. */
static void test_inspect_not_a_cert(void **state)
{
    (void)state;
    ehem_ctx *ctx = make_ctx();
    ehem_cert_info *info = NULL;

    /* base64 of "hello world" — decodes fine, parses as no cert. */
    assert_int_equal(ehem_cert_inspect(ctx, "aGVsbG8gd29ybGQ=", &info),
                     EHEM_ERR_PROTOCOL);
    assert_null(info);

    ehem_ctx_destroy(ctx);
}

/* Argument checks + free NULL-safety. */
static void test_inspect_arg_and_free(void **state)
{
    (void)state;
    ehem_ctx *ctx = make_ctx();
    ehem_cert_info *info = NULL;

    assert_int_equal(ehem_cert_inspect(NULL, EHEM_FX_LEAF_B64, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_cert_inspect(ctx, NULL, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_cert_inspect(ctx, EHEM_FX_LEAF_B64, NULL), EHEM_ERR_ARG);
    assert_int_equal(ehem_cert_inspect(ctx, "", &info), EHEM_ERR_PROTOCOL);
    ehem_cert_info_free(NULL);

    ehem_ctx_destroy(ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_inspect_leaf_fields),
        cmocka_unit_test(test_inspect_bad_base64),
        cmocka_unit_test(test_inspect_not_a_cert),
        cmocka_unit_test(test_inspect_arg_and_free),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
