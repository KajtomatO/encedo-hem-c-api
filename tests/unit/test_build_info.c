/*
 * test_build_info.c — ehem_build_info_get(): the crypto and transport backend
 * names, compiled-against vs loaded versions and link modes.
 *
 * verifies: REQ-API-011 (every string non-NULL; the backend name, the
 *           compiled version and the link mode equal what the build
 *           configured; the libcurl pair filled; identical answers before
 *           ehem_global_init(), after it and after a forced REQ-API-010
 *           refusal; the reserved slots read NULL; a NULL out is a no-op)
 *
 * The expected values come from the build's own dependency discovery
 * (pkg-config's wolfssl module version, FindCURL's version, the configure-time
 * link-mode detection — tests/unit/CMakeLists.txt), an oracle independent of
 * the backend headers the SDK compiles against: nothing under tests/ includes
 * a backend header.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "crypto_shim.h"      /* internal: ehem_crypto_test_force_mismatch */

#ifndef EHEM_TEST_CRYPTO_BACKEND
#error "EHEM_TEST_CRYPTO_BACKEND must come from tests/unit/CMakeLists.txt"
#endif

static void assert_link_word(const char *s)
{
    assert_non_null(s);
    assert_true(strcmp(s, "dynamic") == 0 || strcmp(s, "static") == 0);
}

/* Every field filled with a static string; reserved slots NULL. */
static void assert_filled(const ehem_build_info *bi)
{
    size_t i;
    assert_non_null(bi->crypto_backend);
    assert_non_null(bi->crypto_version_compiled);
    assert_non_null(bi->crypto_version_loaded);
    assert_link_word(bi->crypto_link);
    assert_non_null(bi->transport_backend);
    assert_non_null(bi->transport_version_compiled);
    assert_non_null(bi->transport_version_loaded);
    assert_link_word(bi->transport_link);
    assert_true(bi->crypto_version_loaded[0] != '\0');
    assert_true(bi->transport_version_loaded[0] != '\0');
    for (i = 0; i < sizeof bi->reserved / sizeof bi->reserved[0]; i++) {
        assert_null(bi->reserved[i]);
    }
}

static void assert_same(const ehem_build_info *a, const ehem_build_info *b)
{
    assert_string_equal(a->crypto_backend, b->crypto_backend);
    assert_string_equal(a->crypto_version_compiled, b->crypto_version_compiled);
    assert_string_equal(a->crypto_version_loaded, b->crypto_version_loaded);
    assert_string_equal(a->crypto_link, b->crypto_link);
    assert_string_equal(a->transport_backend, b->transport_backend);
    assert_string_equal(a->transport_version_compiled,
                        b->transport_version_compiled);
    assert_string_equal(a->transport_version_loaded,
                        b->transport_version_loaded);
    assert_string_equal(a->transport_link, b->transport_link);
}

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

/* The values are what the build configured. */
static void test_values_match_the_build(void **state)
{
    (void)state;
    ehem_build_info bi;

    memset(&bi, 0xA5, sizeof bi);           /* every field must be written */
    ehem_build_info_get(&bi);
    assert_filled(&bi);
    assert_string_equal(bi.crypto_backend, EHEM_TEST_CRYPTO_BACKEND);
    assert_string_equal(bi.crypto_version_compiled, EHEM_TEST_CRYPTO_VERSION);
    assert_string_equal(bi.crypto_link, EHEM_TEST_CRYPTO_LINK);
    assert_string_equal(bi.transport_backend, "libcurl");
    assert_string_equal(bi.transport_version_compiled,
                        EHEM_TEST_TRANSPORT_VERSION);
    assert_string_equal(bi.transport_link, EHEM_TEST_TRANSPORT_LINK);
}

/* This process's library passes REQ-API-010's exact-version rule (the whole
 * suite initialises against it), so loaded == compiled here. */
static void test_loaded_crypto_is_the_compiled_one(void **state)
{
    (void)state;
    ehem_build_info bi;

    assert_int_equal(ehem_global_init(), EHEM_OK);
    ehem_build_info_get(&bi);
    assert_string_equal(bi.crypto_version_loaded, bi.crypto_version_compiled);
}

/* Same answer before init, after it, and after a forced refusal of either
 * kind — the refusal message is built from exactly these values. */
static void test_same_before_after_and_refused(void **state)
{
    (void)state;
    ehem_build_info before, after, refused_version, refused_probe;

    ehem_build_info_get(&before);
    assert_filled(&before);

    assert_int_equal(ehem_global_init(), EHEM_OK);
    ehem_build_info_get(&after);
    assert_filled(&after);
    assert_same(&before, &after);
    ehem_global_cleanup();

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    ehem_build_info_get(&refused_version);
    assert_filled(&refused_version);
    assert_same(&before, &refused_version);

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_PROBE);
    assert_int_equal(ehem_global_init(), EHEM_ERR_BACKEND_MISMATCH);
    ehem_build_info_get(&refused_probe);
    assert_filled(&refused_probe);
    assert_same(&before, &refused_probe);
}

/* NULL is a no-op (the function cannot fail). */
static void test_null_is_a_no_op(void **state)
{
    (void)state;
    ehem_build_info_get(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_values_match_the_build,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_loaded_crypto_is_the_compiled_one,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_same_before_after_and_refused,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test(test_null_is_a_no_op),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
