/*
 * Integration tests for HMAC hash and verify.
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"
#include "hem/hem_crypto.h"

static const uint8_t MSG[] = "hello, encedo";
static const size_t  MSG_LEN = sizeof(MSG) - 1;

/* -------------------------------------------------------------------------
 * HMAC hash: same message produces same MAC (deterministic)
 * ---------------------------------------------------------------------- */
static void test_hmac_deterministic(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-hmac", "HMAC-SHA256", kid, sizeof(kid)));

    uint8_t mac1[64] = {0}, mac2[64] = {0};
    size_t  mac1_len = 0,   mac2_len = 0;

    assert_hem_ok(ctx, hem_hmac_hash(ctx, kid, "SHA2-256", MSG, MSG_LEN,
                                     mac1, sizeof(mac1), &mac1_len));
    assert_true(mac1_len > 0);

    assert_hem_ok(ctx, hem_hmac_hash(ctx, kid, "SHA2-256", MSG, MSG_LEN,
                                     mac2, sizeof(mac2), &mac2_len));

    assert_int_equal((int)mac1_len, (int)mac2_len);
    assert_memory_equal(mac1, mac2, mac1_len);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * HMAC verify: valid MAC accepted, corrupted MAC rejected
 * ---------------------------------------------------------------------- */
static void test_hmac_verify(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-hmac-v", "HMAC-SHA256", kid, sizeof(kid)));

    uint8_t mac[64] = {0};
    size_t  mac_len = 0;
    assert_hem_ok(ctx, hem_hmac_hash(ctx, kid, "SHA2-256", MSG, MSG_LEN,
                                     mac, sizeof(mac), &mac_len));

    /* Valid MAC must pass */
    assert_hem_ok(ctx, hem_hmac_verify(ctx, kid, "SHA2-256",
                                        MSG, MSG_LEN, mac, mac_len));

    /* Corrupted MAC must fail */
    mac[0] ^= 0xFF;
    hem_error_t err = hem_hmac_verify(ctx, kid, "SHA2-256",
                                       MSG, MSG_LEN, mac, mac_len);
    assert_true(err != HEM_OK);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_hmac_deterministic, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_hmac_verify,        setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
