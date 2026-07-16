/*
 * test_keymgmt_search_live.c — live descr search against the real dev HEM.
 *
 * verifies: REQ-KEY-002 (a prefix search for a created EHEMTEST key's descr
 *           returns it; a search matching nothing returns an empty result). Uses
 *           the EHEMTEST helper (REQ-TEST-003) for the disposable target and
 *           teardown cleanup.
 *
 * Observed no-match status (firmware v1.2.2, confirmed against
 * encedo_firmware api_keymgmt.c:249-256): the device returns HTTP 200 with an
 * empty list for zero matches (NOT 404 — that was the python client's older-
 * firmware observation); 410 is a filter FAILURE. Either way the binding yields
 * an empty page, which this test asserts.
 *
 * Public API only; skipped (exit 77) unless EHEM_TEST_URL + EHEM_TEST_PASSPHRASE.
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

typedef struct search_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} search_state;

static int setup(void **state)
{
    search_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    search_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static int page_has_kid(const ehem_key_page *page, const char *kid)
{
    size_t i;
    for (i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_search_prefix_and_no_match(void **state)
{
    search_state *s = *state;
    char label[32];
    char kid[EHEM_KID_HEX_SIZE] = {0};
    char nomatch[48];
    ehem_key_create_params p;
    ehem_key_page *page = NULL;
    ehem_rc rc;

    /* A unique EHEMTEST label doubles as the key's descr, so a prefix search
     * for those bytes matches exactly this key. */
    ehem_test_label(label, sizeof label);

    memset(&p, 0, sizeof p);
    p.type = "ED25519";
    p.label = label;
    p.descr = (const uint8_t *)label;
    p.descr_len = strlen(label);

    rc = ehem_key_create(s->ctx, &p, kid);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_create failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid);
    printf("[test_keymgmt_search_live] created kid=%s descr=%s\n", kid, label);

    /* Prefix search for the descr → the created key is in the results. */
    rc = ehem_key_search_all(s->ctx, (const uint8_t *)label, strlen(label),
                             EHEM_KEY_SEARCH_PREFIX, &page);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_search_all(prefix) failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    assert_non_null(page);
    assert_true(page_has_kid(page, kid));
    printf("[test_keymgmt_search_live] prefix search matched %d key(s)\n",
           (int)page->listed);
    ehem_key_page_free(page);
    page = NULL;

    /* A pattern no descr starts with → empty result (EHEM_OK, listed 0). */
    snprintf(nomatch, sizeof nomatch, "NO_SUCH_DESCR_%s", label);
    rc = ehem_key_search_all(s->ctx, (const uint8_t *)nomatch, strlen(nomatch),
                             EHEM_KEY_SEARCH_PREFIX, &page);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_search_all(no-match) failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    assert_non_null(page);
    assert_int_equal((int)page->listed, 0);
    printf("[test_keymgmt_search_live] no-match search returned empty (OK)\n");
    ehem_key_page_free(page);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live keymgmt search test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_search_prefix_and_no_match, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
