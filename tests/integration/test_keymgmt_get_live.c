/*
 * test_keymgmt_get_live.c — live single-key read + a per-KID scope probe.
 *
 * verifies: REQ-KEY-003 (ehem_key_get of a created EHEMTEST ED25519 key returns
 *           a 32-byte pubkey + its descr), and RECORDS the open scope probe:
 *           does firmware v1.2.2 accept the documented keymgmt:get / keymgmt:gen
 *           scopes for this endpoint, or still only keymgmt:use:<kid>? (§12 risk
 *           3 — the first concrete per-KID-scope fact for M4.)
 *
 * The probe needs to request scopes the public binding never uses, so this test
 * links the STATIC lib + internal headers (via ehem_test_support) — the same
 * exception test_auth_live takes. Skipped (exit 77) unless EHEM_TEST_URL +
 * EHEM_TEST_PASSPHRASE.
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
#include "proto_common.h"     /* internal: ehem_proto_request_json for the probe */
#include "json.h"             /* internal: ehem_json_free */
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct get_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} get_state;

static int setup(void **state)
{
    get_state *s = calloc(1, sizeof *s);
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
    get_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

/* Probe one scope against GET /api/keymgmt/get/{kid} via the internal request
 * path; print the observed rc + HTTP status. Read-only, so harmless. */
static void probe_scope(ehem_ctx *ctx, const char *kid, const char *scope)
{
    char path[64];
    ehem_json *root = NULL;
    ehem_rc rc;

    snprintf(path, sizeof path, "/api/keymgmt/get/%s", kid);
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, path, NULL, scope,
                                 EHEM_TLS_REQ_DEFAULT, &root);
    printf("[test_keymgmt_get_live] SCOPE PROBE %-16s -> %s (HTTP %ld)\n",
           scope, ehem_rc_str(rc), ehem_last_error(ctx)->http_status);
    if (root != NULL) {
        char *dump = ehem_json_print(root);
        printf("[test_keymgmt_get_live]   body: %s\n", dump ? dump : "(print failed)");
        ehem_json_string_free(dump);
    }
    ehem_json_free(root);
}

static void test_get_and_scope_probe(void **state)
{
    get_state *s = *state;
    char label[32];
    char kid[EHEM_KID_HEX_SIZE] = {0};
    const uint8_t descr[] = { 0x11, 0x22, 0x33, 0x44 };
    ehem_key_create_params p;
    ehem_key_details *d = NULL;
    ehem_rc rc;

    ehem_test_label(label, sizeof label);
    memset(&p, 0, sizeof p);
    p.type = "ED25519";
    p.label = label;
    p.descr = descr;
    p.descr_len = sizeof descr;

    rc = ehem_key_create(s->ctx, &p, kid);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_create failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid);

    /* The shipping path: exact per-key scope keymgmt:use:<kid>. */
    rc = ehem_key_get(s->ctx, kid, &d);
    if (rc != EHEM_OK) {
        fail_msg("ehem_key_get failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(s->ctx)->message);
    }
    assert_non_null(d);
    assert_string_equal(d->type, "ED25519");
    assert_int_equal((int)d->pubkey_len, 32);     /* ED25519 raw public key */
    assert_non_null(d->pubkey);
    assert_null(d->der);
    /* fw v1.2.2 quirk: GET omits descr even for a key that has one — the wire
     * body is {type,pubkey,updated} only. descr is retrievable via list/search
     * (proven in test_keymgmt_mutate_live), NOT via get. Confirmed against the
     * firmware source (REPO_GetKey_byKID(...,0,...) yields descr_len 0). The
     * binding therefore returns descr absent, which is what we assert. */
    assert_int_equal((int)d->descr_len, 0);
    assert_null(d->descr);
    printf("[test_keymgmt_get_live] get OK: type=%s pubkey_len=%d descr_len=%d (descr not returned by GET on fw v1.2.2)\n",
           d->type, (int)d->pubkey_len, (int)d->descr_len);
    ehem_key_details_free(d);

    /* OPEN PROBE (REQ-KEY-003 / §12 risk 3): the documented alternatives. The
     * shipping scope keymgmt:use:<kid> is already proven by the get above. */
    probe_scope(s->ctx, kid, "keymgmt:get");
    probe_scope(s->ctx, kid, "keymgmt:gen");
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live keymgmt get test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_get_and_scope_probe, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
