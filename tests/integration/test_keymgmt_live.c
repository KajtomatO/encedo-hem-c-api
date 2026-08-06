/*
 * test_keymgmt_live.c — live key-inventory walk against the real dev-machine HEM.
 *
 * verifies: REQ-KEY-001 (ehem_key_list_all end-to-end: login + a scoped
 *           keymgmt:list walk returns the device's whole key population — at
 *           least the protected TLS pair — with kid/type populated). Public API
 *           only (ehem_login + ehem_key_list_all), so it links the shared lib
 *           like the other integration tests.
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
#include "ehem/keymgmt.h"
#include "integration_env.h"

static void test_list_all_population(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_key_page *page = NULL;
    ehem_rc rc;
    size_t i;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_non_null(ctx);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_list_all failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(page);

    /* The repository always holds at least the device's protected TLS pair, so
     * a healthy device reports a non-empty population, all merged into one page. */
    assert_true(page->listed > 0);
    assert_int_equal((int)page->listed, (int)page->total);
    assert_non_null(page->entries);

    printf("[test_keymgmt_live] total=%d listed=%d\n",
           (int)page->total, (int)page->listed);

    for (i = 0; i < page->listed; i++) {
        const ehem_key_entry *e = &page->entries[i];
        /* kid and type are the required fields — always populated. */
        assert_non_null(e->kid);
        assert_int_equal((int)strlen(e->kid), 32);
        assert_non_null(e->type);
        printf("  [%zu] kid=%s type=%s label=%s descr_len=%zu\n",
               i, e->kid, e->type, e->label ? e->label : "(none)", e->descr_len);
    }

    ehem_key_page_free(page);
    assert_int_equal(ehem_logout(ctx), EHEM_OK);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live keymgmt test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_list_all_population),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
