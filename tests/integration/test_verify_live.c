/*
 * test_verify_live.c — live device-side signature verification: sign with an
 * EHEMTEST key via ehem_sign, verify via ehem_verify, then prove the negative
 * paths (tampered signature, wrong message) are rejected with 406.
 *
 * verifies: REQ-OPS-003 (live: sign → device verify EHEM_OK; one flipped
 *           signature bit → EHEM_ERR_DEVICE http 406; truncated msg →
 *           EHEM_ERR_DEVICE 406; sign and verify ride ONE per-KID token —
 *           asserted indirectly: verify succeeds without a second login
 *           being possible to observe here, the unit suite pins the count)
 *
 * PUBLIC API only (shared library), like test_keymgmt_mutate_live. Skips
 * (exit 77) without EHEM_TEST_URL/PASSPHRASE. EHEMTEST key hygiene per
 * REQ-TEST-003 (tracked cleanup, pass-or-fail).
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
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct verify_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} verify_state;

static int setup(void **state)
{
    verify_state *s = calloc(1, sizeof *s);
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
    verify_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static const uint8_t MSG[] = "verified by test_verify_live via the Encedo HEM";
#define MSG_LEN (sizeof MSG - 1)

static void test_sign_then_device_verify(void **state)
{
    verify_state *s = *state;
    ehem_key_create_params p;
    char kid[EHEM_KID_HEX_SIZE];
    char label[32];
    ehem_signature *sig = NULL;

    memset(&p, 0, sizeof p);
    ehem_test_label(label, sizeof label);
    p.type = "ED25519";
    p.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &p, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);

    assert_int_equal(ehem_sign(s->ctx, kid, EHEM_SIGN_ALG_ED25519,
                               MSG, MSG_LEN, NULL, 0, &sig), EHEM_OK);
    assert_non_null(sig);
    assert_int_equal((int)sig->sig_len, 64);

    /* The device confirms its own signature. */
    assert_int_equal(ehem_verify(s->ctx, kid, EHEM_SIGN_ALG_ED25519,
                                 MSG, MSG_LEN, NULL, 0,
                                 sig->sig, sig->sig_len), EHEM_OK);

    /* One flipped bit → invalid → 406 → EHEM_ERR_DEVICE (REQ-OPS-003). */
    sig->sig[10] ^= 0x01;
    assert_int_equal(ehem_verify(s->ctx, kid, EHEM_SIGN_ALG_ED25519,
                                 MSG, MSG_LEN, NULL, 0,
                                 sig->sig, sig->sig_len), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);
    sig->sig[10] ^= 0x01;

    /* Right signature, wrong (truncated) message → 406 as well. */
    assert_int_equal(ehem_verify(s->ctx, kid, EHEM_SIGN_ALG_ED25519,
                                 MSG, MSG_LEN - 1, NULL, 0,
                                 sig->sig, sig->sig_len), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);

    ehem_signature_free(sig);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sign_then_device_verify),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
