/*
 * Integration tests for AES encrypt/decrypt/wrap/unwrap.
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"
#include "hem/hem_crypto.h"

static const uint8_t PLAINTEXT[]  = "The quick brown fox jumps over the lazy dog.";
static const size_t  PT_LEN       = sizeof(PLAINTEXT) - 1;

/* -------------------------------------------------------------------------
 * AES256-GCM encrypt / decrypt roundtrip
 * ---------------------------------------------------------------------- */
static void test_aes_gcm_roundtrip(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-aes-gcm", "AES256", kid, sizeof(kid)));

    /* Encrypt */
    uint8_t ct_buf[256] = {0};
    hem_cipher_result_t enc = {0};
    assert_hem_ok(ctx, hem_encrypt(ctx, kid, "AES256-GCM",
                                   PLAINTEXT, PT_LEN,
                                   NULL, 0,
                                   ct_buf, sizeof(ct_buf), &enc));
    assert_true(enc.ciphertext_len > 0);
    assert_true(enc.iv_len == 12 || enc.iv_len == 16);
    assert_true(enc.tag_len == 16);

    /* Decrypt */
    uint8_t pt_buf[256] = {0};
    size_t  pt_len = 0;
    assert_hem_ok(ctx, hem_decrypt(ctx, kid, "AES256-GCM",
                                   enc.ciphertext, enc.ciphertext_len,
                                   enc.iv, enc.iv_len,
                                   enc.tag, enc.tag_len,
                                   NULL, 0,
                                   pt_buf, sizeof(pt_buf), &pt_len));

    assert_int_equal((int)pt_len, (int)PT_LEN);
    assert_memory_equal(pt_buf, PLAINTEXT, PT_LEN);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * AES256-CBC encrypt / decrypt roundtrip
 * ---------------------------------------------------------------------- */
static void test_aes_cbc_roundtrip(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-aes-cbc", "AES256", kid, sizeof(kid)));

    uint8_t ct_buf[256] = {0};
    hem_cipher_result_t enc = {0};
    assert_hem_ok(ctx, hem_encrypt(ctx, kid, "AES256-CBC",
                                   PLAINTEXT, PT_LEN,
                                   NULL, 0,
                                   ct_buf, sizeof(ct_buf), &enc));
    assert_true(enc.ciphertext_len > 0);
    assert_true(enc.iv_len == 16);

    uint8_t pt_buf[256] = {0};
    size_t  pt_len = 0;
    assert_hem_ok(ctx, hem_decrypt(ctx, kid, "AES256-CBC",
                                   enc.ciphertext, enc.ciphertext_len,
                                   enc.iv, enc.iv_len,
                                   NULL, 0,
                                   NULL, 0,
                                   pt_buf, sizeof(pt_buf), &pt_len));

    assert_int_equal((int)pt_len, (int)PT_LEN);
    assert_memory_equal(pt_buf, PLAINTEXT, PT_LEN);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * AES key wrap / unwrap roundtrip (RFC 3394)
 * ---------------------------------------------------------------------- */
static void test_wrap_unwrap(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-wrap-key", "AES256", kid, sizeof(kid)));

    /* 32 bytes of key material (multiple of 8, >= 16) */
    uint8_t key_material[32];
    for (int i = 0; i < 32; i++) key_material[i] = (uint8_t)(i + 1);

    uint8_t wrapped[64] = {0};
    size_t  wrapped_len = 0;
    assert_hem_ok(ctx, hem_key_wrap(ctx, kid, "AES256",
                                    key_material, sizeof(key_material),
                                    wrapped, sizeof(wrapped), &wrapped_len));
    assert_int_equal((int)wrapped_len, 40);  /* 32 + 8 RFC 3394 overhead */

    uint8_t unwrapped[32] = {0};
    size_t  unwrapped_len = 0;
    assert_hem_ok(ctx, hem_key_unwrap(ctx, kid, "AES256",
                                      wrapped, wrapped_len,
                                      unwrapped, sizeof(unwrapped), &unwrapped_len));
    assert_int_equal((int)unwrapped_len, 32);
    assert_memory_equal(unwrapped, key_material, 32);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_aes_gcm_roundtrip, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_aes_cbc_roundtrip, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_wrap_unwrap,        setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
