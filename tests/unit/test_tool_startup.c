/*
 * test_tool_startup.c — hem-tool's start of run (hem-tool-core startup.c):
 * the explicit SDK initialisation with its refusal message and exit code,
 * and the --version report.
 *
 * verifies: REQ-TOOL-024 (a forced REQ-API-010 refusal — version or probe —
 *           gives ONE message naming the crypto backend, the version
 *           hem-tool was built against, the loaded one and the remedy, and
 *           exit 12; the matching library initialises silently),
 *           REQ-TOOL-025 (the three-line report: `hem-tool <version>`, the
 *           crypto line and the transport line, values equal to
 *           ehem_version() / ehem_build_info_get(), ASCII; works under a
 *           forced mismatch)
 *
 * The command-line ordering (help / --version before the init, `status`
 * refused before any traffic) is proven on the real main.c by the
 * tool_startup_cli test (check_tool_startup.cmake).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "crypto_shim.h"      /* internal: ehem_crypto_test_force_mismatch */
#include "startup.h"

static char *slurp(FILE *f)
{
    long n;
    char *buf;
    assert_int_equal(fseek(f, 0, SEEK_END), 0);
    n = ftell(f);
    assert_true(n >= 0);
    rewind(f);
    buf = malloc((size_t)n + 1);
    assert_non_null(buf);
    assert_int_equal(fread(buf, 1, (size_t)n, f), (size_t)n);
    buf[n] = '\0';
    return buf;
}

static size_t count(const char *hay, const char *needle)
{
    size_t n = 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

static void assert_ascii(const char *s)
{
    for (; *s != '\0'; s++) {
        assert_true((unsigned char)*s < 0x80);
    }
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

/* Run hem_tool_global_init under `mode`; return its stderr text. */
static char *init_under(int mode, int expect_ret)
{
    FILE *err = tmpfile();
    char *s;
    assert_non_null(err);
    ehem_crypto_test_force_mismatch(mode);
    assert_int_equal(hem_tool_global_init(err), expect_ret);
    s = slurp(err);
    fclose(err);
    return s;
}

/* The matching library: initialised, nothing printed. */
static void test_init_ok_is_silent(void **state)
{
    (void)state;
    char *s = init_under(EHEM_CRYPTO_FORCE_NONE, 0);
    assert_string_equal(s, "");
    free(s);
}

/* A version refusal: one message with backend, both versions, the remedy. */
static void test_version_refusal_message(void **state)
{
    (void)state;
    ehem_build_info bi;
    char *s = init_under(EHEM_CRYPTO_FORCE_VERSION, HEM_TOOL_EXIT_INIT);

    ehem_build_info_get(&bi);
    assert_int_equal(HEM_TOOL_EXIT_INIT, 12);
    assert_int_equal(count(s, "error:"), 1);          /* ONE message */
    assert_non_null(strstr(s, "EHEM_ERR_BACKEND_MISMATCH"));
    assert_true(count(s, bi.crypto_backend) >= 2);
    assert_non_null(strstr(s, "built against"));
    assert_non_null(strstr(s, bi.crypto_version_compiled));
    assert_non_null(strstr(s, "loaded"));
    assert_non_null(strstr(s, bi.crypto_version_loaded));
    assert_non_null(strstr(s, "remedy:"));
    assert_non_null(strstr(s, "-static hem-tool"));
    if (strcmp(bi.crypto_backend, "wolfSSL") == 0) {
        /* the companion asset, by its release name */
        char asset[64];
        snprintf(asset, sizeof asset, "wolfssl-%s-", bi.crypto_version_compiled);
        assert_non_null(strstr(s, asset));
    }
    assert_ascii(s);
    free(s);
}

/* A probe refusal (same version, other build): same message shape, plus the
 * note that explains equal versions. */
static void test_probe_refusal_message(void **state)
{
    (void)state;
    char *s = init_under(EHEM_CRYPTO_FORCE_PROBE, HEM_TOOL_EXIT_INIT);

    assert_int_equal(count(s, "error:"), 1);
    assert_non_null(strstr(s, "EHEM_ERR_BACKEND_MISMATCH"));
    assert_non_null(strstr(s, "same version, incompatible build"));
    assert_non_null(strstr(s, "remedy:"));
    assert_ascii(s);
    free(s);
}

/* The refusal leaves the SDK uninitialised: a later init (seam cleared)
 * succeeds — the tool's exit path needs no cleanup. */
static void test_refusal_then_ok(void **state)
{
    (void)state;
    char *s = init_under(EHEM_CRYPTO_FORCE_VERSION, HEM_TOOL_EXIT_INIT);
    free(s);
    s = init_under(EHEM_CRYPTO_FORCE_NONE, 0);
    assert_string_equal(s, "");
    free(s);
}

/* --version: three lines, values from ehem_version() + ehem_build_info_get();
 * works under a forced mismatch (it never initialises). */
static void test_version_report(void **state)
{
    (void)state;
    ehem_build_info bi;
    char expect[256];
    char *s;
    FILE *out = tmpfile();
    assert_non_null(out);

    ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    hem_tool_print_version(out);
    s = slurp(out);
    fclose(out);

    ehem_build_info_get(&bi);
    assert_int_equal(count(s, "\n"), 3);
    snprintf(expect, sizeof expect, "hem-tool %s\n", ehem_version());
    assert_memory_equal(s, expect, strlen(expect));     /* first line, exact */
    snprintf(expect, sizeof expect,
             "crypto:    %s %s (compiled) / %s (loaded), %s\n",
             bi.crypto_backend, bi.crypto_version_compiled,
             bi.crypto_version_loaded, bi.crypto_link);
    assert_non_null(strstr(s, expect));
    snprintf(expect, sizeof expect,
             "transport: %s %s (compiled) / %s (loaded), %s\n",
             bi.transport_backend, bi.transport_version_compiled,
             bi.transport_version_loaded, bi.transport_link);
    assert_non_null(strstr(s, expect));
    assert_ascii(s);
    free(s);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_init_ok_is_silent,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_version_refusal_message,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_probe_refusal_message,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_refusal_then_ok,
                                        setup_not_ready, teardown_restore),
        cmocka_unit_test_setup_teardown(test_version_report,
                                        setup_not_ready, teardown_restore),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
