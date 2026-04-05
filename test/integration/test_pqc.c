/*
 * Integration tests for post-quantum cryptography (ML-KEM and ML-DSA).
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"
#include "hem/hem_pqc.h"

static const uint8_t MSG[]   = "post-quantum test message";
static const size_t  MSG_LEN = sizeof(MSG) - 1;

/* -------------------------------------------------------------------------
 * ML-KEM768: encaps → decaps shared secret roundtrip
 * ---------------------------------------------------------------------- */
static void test_mlkem768(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-mlkem768", "MLKEM768", kid, sizeof(kid)));

    /* Encapsulate */
    uint8_t ss_enc[32]   = {0};
    uint8_t ct[1200]     = {0};  /* MLKEM768 ciphertext: 1088 bytes */
    size_t  ss_enc_len   = 0;
    size_t  ct_len       = 0;

    assert_hem_ok(ctx, hem_mlkem_encaps(ctx, kid,
                                         ss_enc, sizeof(ss_enc), &ss_enc_len,
                                         ct, sizeof(ct), &ct_len));
    assert_int_equal((int)ss_enc_len, 32);
    assert_int_equal((int)ct_len, 1088);

    /* Decapsulate */
    uint8_t ss_dec[32] = {0};
    size_t  ss_dec_len = 0;
    assert_hem_ok(ctx, hem_mlkem_decaps(ctx, kid,
                                         ct, ct_len,
                                         ss_dec, sizeof(ss_dec), &ss_dec_len));
    assert_int_equal((int)ss_dec_len, 32);

    /* Shared secrets must match */
    assert_memory_equal(ss_enc, ss_dec, 32);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * ML-KEM512: smaller variant
 * ---------------------------------------------------------------------- */
static void test_mlkem512(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-mlkem512", "MLKEM512", kid, sizeof(kid)));

    uint8_t ss_enc[32] = {0}, ct[800] = {0};
    size_t  ss_enc_len = 0, ct_len = 0;

    assert_hem_ok(ctx, hem_mlkem_encaps(ctx, kid,
                                         ss_enc, sizeof(ss_enc), &ss_enc_len,
                                         ct, sizeof(ct), &ct_len));
    assert_int_equal((int)ct_len, 768);

    uint8_t ss_dec[32] = {0};
    size_t  ss_dec_len = 0;
    assert_hem_ok(ctx, hem_mlkem_decaps(ctx, kid, ct, ct_len,
                                         ss_dec, sizeof(ss_dec), &ss_dec_len));

    assert_memory_equal(ss_enc, ss_dec, 32);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * ML-DSA65: sign → verify OK, corrupt sig → verify fails
 * ---------------------------------------------------------------------- */
static void test_mldsa65(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-mldsa65", "MLDSA65", kid, sizeof(kid)));

    uint8_t sig[3400] = {0};  /* MLDSA65 signature: 3309 bytes */
    size_t  sig_len = 0;

    assert_hem_ok(ctx, hem_mldsa_sign(ctx, kid, MSG, MSG_LEN,
                                       sig, sizeof(sig), &sig_len));
    assert_int_equal((int)sig_len, 3309);

    /* Valid signature must verify */
    assert_hem_ok(ctx, hem_mldsa_verify(ctx, kid, MSG, MSG_LEN, sig, sig_len));

    /* Corrupted signature must fail */
    sig[0] ^= 0xFF;
    hem_error_t err = hem_mldsa_verify(ctx, kid, MSG, MSG_LEN, sig, sig_len);
    assert_true(err != HEM_OK);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_mlkem768, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_mlkem512, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_mldsa65,  setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
