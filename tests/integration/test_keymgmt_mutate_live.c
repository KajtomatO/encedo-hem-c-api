/*
 * test_keymgmt_mutate_live.c — live create → list → delete round-trip against
 * the real dev-machine HEM.
 *
 * verifies: REQ-KEY-005 (create an EHEMTEST ED25519 key with a descr → the
 *           returned kid appears in the listing with that label + descr),
 *           REQ-KEY-004 (delete it → it is gone from the listing; deleting the
 *           same kid again → EHEM_ERR_NOT_FOUND), REQ-TEST-003 (EHEMTEST-prefixed
 *           labels via the shared helper, teardown cleanup that runs pass-or-fail,
 *           a leftover sweep in setup).
 *
 * Public API only (login + keymgmt bindings), links the shared library. Skipped
 * (exit 77) unless EHEM_TEST_URL and EHEM_TEST_PASSPHRASE are set (REQ-TEST-002).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/keymgmt.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

/* Per-test state shared with the teardown so cleanup runs even when the body
 * fails (cmocka invokes teardown after a failed test). */
typedef struct mut_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} mut_state;

static int setup(void **state)
{
    mut_state *s = calloc(1, sizeof *s);
    int swept;
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    /* Clear any EHEMTEST leftovers from an interrupted earlier run. */
    swept = ehem_test_sweep(s->ctx);
    if (swept > 0) {
        printf("[test_keymgmt_mutate_live] swept %d leftover EHEMTEST key(s)\n", swept);
    }
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    mut_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);   /* pass or fail */
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void test_create_list_delete(void **state)
{
    mut_state *s = *state;
    char label[32];
    const uint8_t descr[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };
    ehem_key_create_params p;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_key_page *page = NULL;
    const ehem_key_entry *found;
    ehem_rc rc;
    size_t i;

    ehem_test_label(label, sizeof label);

    memset(&p, 0, sizeof p);
    p.type = "ED25519";
    p.label = label;
    p.descr = descr;
    p.descr_len = sizeof descr;

    /* Create. */
    rc = ehem_key_create(s->ctx, &p, kid);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_create failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid);            /* register for cleanup NOW */
    assert_int_equal((int)strlen(kid), 32);
    printf("[test_keymgmt_mutate_live] created label=%s kid=%s\n", label, kid);

    /* It appears in the full listing with the label + descr we sent. */
    assert_int_equal(ehem_key_list_all(s->ctx, &page), EHEM_OK);
    found = NULL;
    for (i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            found = &page->entries[i];
            break;
        }
    }
    assert_non_null(found);
    assert_non_null(found->label);
    assert_string_equal(found->label, label);
    assert_int_equal((int)found->descr_len, (int)sizeof descr);
    assert_memory_equal(found->descr, descr, sizeof descr);
    ehem_key_page_free(page);
    page = NULL;

    /* Delete → gone from the listing. */
    rc = ehem_key_delete(s->ctx, kid);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_delete failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    assert_int_equal(ehem_key_list_all(s->ctx, &page), EHEM_OK);
    for (i = 0; i < page->listed; i++) {
        assert_int_not_equal(strcmp(page->entries[i].kid, kid), 0);
    }
    ehem_key_page_free(page);

    /* Second delete of the same kid → NOT_FOUND. */
    assert_int_equal(ehem_key_delete(s->ctx, kid), EHEM_ERR_NOT_FOUND);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live keymgmt mutate test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_list_delete, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
