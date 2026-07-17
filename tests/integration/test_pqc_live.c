/*
 * test_pqc_live.c — live ML-KEM and ML-DSA: KEM round-trip (encaps → decaps
 * same ss), ML-DSA sign → device verify ± ctx, size checks per parameter
 * set, and the two REQ-OPS-007/008 firmware-quirk probes (decaps `alg`
 * value; failed-verify raw HTTP status).
 *
 * verifies: REQ-OPS-007 (live: MLKEM768 encaps → 32B ss + 1088B ct + alg
 *           "MLKEM768"; decaps(ct) → identical ss; truncated ct → 406;
 *           MLKEM512 round-trip with 768B ct; PROBE: the decaps `alg`
 *           value is recorded),
 *           REQ-OPS-008 (live: MLDSA65 sign → 3309B sig + alg "MLDSA65" →
 *           device verify EHEM_OK; ctx round-trip + cross-ctx verify
 *           fails; MLDSA44 round-trip with 2420B sig; PROBE: the HTTP
 *           status of an invalid-signature verify is recorded)
 *
 * PUBLIC API only, but uses the longer-timeout context helper (ML-DSA
 * keygen is slow). EHEMTEST hygiene per REQ-TEST-003.
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

typedef struct pqc_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} pqc_state;

static int setup(void **state)
{
    pqc_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    /* 120 s whole-request timeout: ML-DSA keygen is the slowest device op
     * (M5 matrix finding). */
    if (ehem_test_ctx_timeout(&s->ctx, 120000) != EHEM_OK ||
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
    pqc_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void create_key(pqc_state *s, const char *type,
                       char kid[EHEM_KID_HEX_SIZE])
{
    ehem_key_create_params p;
    char label[32];

    memset(&p, 0, sizeof p);
    ehem_test_label(label, sizeof label);
    p.type = type;
    p.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &p, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);
}

static const uint8_t MSG[] = "pqc-signed by test_pqc_live on the HEM";
#define MSG_LEN (sizeof MSG - 1)

/* -------------------------------------------------------------------------- */
/* ML-KEM                                                                      */
/* -------------------------------------------------------------------------- */

static void test_mlkem_roundtrips_and_alg_probe(void **state)
{
    pqc_state *s = *state;
    char kid768[EHEM_KID_HEX_SIZE], kid512[EHEM_KID_HEX_SIZE];
    ehem_mlkem_encaps_result *er = NULL;
    ehem_mlkem_secret *ds = NULL;

    /* MLKEM768: the full contract. */
    create_key(s, "MLKEM768", kid768);
    assert_int_equal(ehem_mlkem_encaps(s->ctx, kid768, &er), EHEM_OK);
    assert_string_equal(er->alg, "MLKEM768");
    assert_int_equal((int)er->ct_len, 1088);

    assert_int_equal(ehem_mlkem_decaps(s->ctx, kid768, er->ct, er->ct_len,
                                       &ds), EHEM_OK);
    assert_memory_equal(ds->ss, er->ss, EHEM_MLKEM_SS_LEN);

    /* REQ-OPS-007 PROBE: what does decaps put in `alg` on fw v1.2.2?
     * (The handler echoes a buffer CRYPTO_MLKEM_Decaps never writes.) */
    printf("REQ-OPS-007 PROBE: decaps alg = \"%s\" (encaps said MLKEM768)\n",
           ds->alg);
    ehem_mlkem_secret_free(ds);
    ds = NULL;

    /* Truncated ct → 406. */
    assert_int_equal(ehem_mlkem_decaps(s->ctx, kid768, er->ct,
                                       er->ct_len - 16, &ds),
                     EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);
    ehem_mlkem_encaps_result_free(er);
    er = NULL;

    /* A second parameter set: MLKEM512 → 768-byte ct. */
    create_key(s, "MLKEM512", kid512);
    assert_int_equal(ehem_mlkem_encaps(s->ctx, kid512, &er), EHEM_OK);
    assert_string_equal(er->alg, "MLKEM512");
    assert_int_equal((int)er->ct_len, 768);
    assert_int_equal(ehem_mlkem_decaps(s->ctx, kid512, er->ct, er->ct_len,
                                       &ds), EHEM_OK);
    assert_memory_equal(ds->ss, er->ss, EHEM_MLKEM_SS_LEN);

    ehem_mlkem_secret_free(ds);
    ehem_mlkem_encaps_result_free(er);
}

/* -------------------------------------------------------------------------- */
/* ML-DSA                                                                      */
/* -------------------------------------------------------------------------- */

static void test_mldsa_roundtrips_ctx_and_status_probe(void **state)
{
    pqc_state *s = *state;
    char kid65[EHEM_KID_HEX_SIZE], kid44[EHEM_KID_HEX_SIZE];
    ehem_mldsa_signature *sig = NULL;
    static const uint8_t sctx[5] = { 'p', 'q', 'c', '-', '1' };
    static const uint8_t sctx2[5] = { 'p', 'q', 'c', '-', '2' };
    ehem_rc rc;
    long status;

    /* MLDSA65: sign → device verify. */
    create_key(s, "MLDSA65", kid65);
    assert_int_equal(ehem_mldsa_sign(s->ctx, kid65, MSG, MSG_LEN, NULL, 0,
                                     &sig), EHEM_OK);
    assert_string_equal(sig->alg, "MLDSA65");
    assert_int_equal((int)sig->sig_len, 3309);
    assert_int_equal(ehem_mldsa_verify(s->ctx, kid65, MSG, MSG_LEN, NULL, 0,
                                       sig->sig, sig->sig_len), EHEM_OK);

    /* REQ-OPS-008 PROBE: flip one bit — what status does a FAILED verify
     * really produce on fw v1.2.2? (Doc says 406; the code path emits the
     * raw crypto error.) The SDK must report EHEM_ERR_DEVICE either way. */
    sig->sig[100] ^= 0x01;
    rc = ehem_mldsa_verify(s->ctx, kid65, MSG, MSG_LEN, NULL, 0,
                           sig->sig, sig->sig_len);
    status = ehem_last_error(s->ctx)->http_status;
    printf("REQ-OPS-008 PROBE: invalid-signature verify → rc=%d, "
           "HTTP status %ld (doc claimed 406)\n", (int)rc, status);
    assert_int_equal(rc, EHEM_ERR_DEVICE);
    assert_int_not_equal(status, 200);
    sig->sig[100] ^= 0x01;
    ehem_mldsa_signature_free(sig);
    sig = NULL;

    /* ctx round-trip + cross-ctx failure. */
    assert_int_equal(ehem_mldsa_sign(s->ctx, kid65, MSG, MSG_LEN,
                                     sctx, sizeof sctx, &sig), EHEM_OK);
    assert_int_equal(ehem_mldsa_verify(s->ctx, kid65, MSG, MSG_LEN,
                                       sctx, sizeof sctx,
                                       sig->sig, sig->sig_len), EHEM_OK);
    assert_int_equal(ehem_mldsa_verify(s->ctx, kid65, MSG, MSG_LEN,
                                       sctx2, sizeof sctx2,
                                       sig->sig, sig->sig_len),
                     EHEM_ERR_DEVICE);
    ehem_mldsa_signature_free(sig);
    sig = NULL;

    /* A second parameter set: MLDSA44 → 2420-byte signature. */
    create_key(s, "MLDSA44", kid44);
    assert_int_equal(ehem_mldsa_sign(s->ctx, kid44, MSG, MSG_LEN, NULL, 0,
                                     &sig), EHEM_OK);
    assert_string_equal(sig->alg, "MLDSA44");
    assert_int_equal((int)sig->sig_len, 2420);
    assert_int_equal(ehem_mldsa_verify(s->ctx, kid44, MSG, MSG_LEN, NULL, 0,
                                       sig->sig, sig->sig_len), EHEM_OK);

    ehem_mldsa_signature_free(sig);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mlkem_roundtrips_and_alg_probe),
        cmocka_unit_test(test_mldsa_roundtrips_ctx_and_status_probe),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
