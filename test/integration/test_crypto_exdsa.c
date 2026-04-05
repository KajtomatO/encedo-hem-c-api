/*
 * Integration tests for ExDSA sign and verify (ECDSA / EdDSA).
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"
#include "hem/hem_crypto.h"

static const uint8_t MSG[]    = "sign me please";
static const size_t  MSG_LEN  = sizeof(MSG) - 1;

/* -------------------------------------------------------------------------
 * Helper: sign → verify OK, then corrupt sig → verify fails
 * ---------------------------------------------------------------------- */
static void sign_verify_roundtrip(hem_ctx_t *ctx, const char *kid,
                                   const char *alg,
                                   uint8_t *sig_buf, size_t sig_buf_size)
{
    size_t sig_len = 0;
    assert_hem_ok(ctx, hem_sign(ctx, kid, alg,
                                MSG, MSG_LEN, NULL, 0,
                                sig_buf, sig_buf_size, &sig_len));
    assert_true(sig_len > 0);

    /* Valid signature must verify */
    assert_hem_ok(ctx, hem_verify(ctx, kid, alg,
                                   MSG, MSG_LEN,
                                   sig_buf, sig_len,
                                   NULL, 0));

    /* Corrupt first byte -- must fail */
    sig_buf[0] ^= 0xFF;
    hem_error_t err = hem_verify(ctx, kid, alg,
                                  MSG, MSG_LEN,
                                  sig_buf, sig_len,
                                  NULL, 0);
    assert_true(err != HEM_OK);
}

/* -------------------------------------------------------------------------
 * ED25519
 * ---------------------------------------------------------------------- */
static void test_ed25519(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-ed25519", "ED25519", kid, sizeof(kid)));

    uint8_t sig[128] = {0};
    sign_verify_roundtrip(ctx, kid, "Ed25519", sig, sizeof(sig));

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * SECP256R1 with SHA256WithECDSA
 * ---------------------------------------------------------------------- */
static void test_secp256r1(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-secp256r1", "SECP256R1", kid, sizeof(kid)));

    uint8_t sig[256] = {0};  /* DER-encoded ECDSA can be up to ~72 bytes */
    sign_verify_roundtrip(ctx, kid, "SHA256WithECDSA", sig, sizeof(sig));

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_ed25519,   setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_secp256r1, setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
