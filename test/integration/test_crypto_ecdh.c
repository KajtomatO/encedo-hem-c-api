/*
 * Integration tests for ECDH key agreement.
 * Creates two on-device keys, performs ECDH in both directions, and
 * verifies the shared secrets are identical.
 */
#include <string.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"
#include "hem/hem_crypto.h"

/* -------------------------------------------------------------------------
 * ECDH symmetry: X25519(A, pubB) == X25519(B, pubA)
 * ---------------------------------------------------------------------- */
static void test_ecdh_symmetric(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid_a[33] = {0}, kid_b[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "ecdh-a", "CURVE25519", kid_a, sizeof(kid_a)));
    assert_hem_ok(ctx, hem_key_create(ctx, "ecdh-b", "CURVE25519", kid_b, sizeof(kid_b)));

    /* Retrieve public keys */
    hem_key_info_t info_a = {0}, info_b = {0};
    assert_hem_ok(ctx, hem_key_get(ctx, kid_a, &info_a));
    assert_hem_ok(ctx, hem_key_get(ctx, kid_b, &info_b));
    assert_true(info_a.pubkey[0] != '\0');
    assert_true(info_b.pubkey[0] != '\0');

    /* ECDH(A, pubB) */
    uint8_t secret_ab[64] = {0};
    size_t  secret_ab_len = 0;
    assert_hem_ok(ctx, hem_ecdh(ctx, kid_a, info_b.pubkey, NULL, NULL,
                                 secret_ab, sizeof(secret_ab), &secret_ab_len));
    assert_true(secret_ab_len == 32);

    /* ECDH(B, pubA) */
    uint8_t secret_ba[64] = {0};
    size_t  secret_ba_len = 0;
    assert_hem_ok(ctx, hem_ecdh(ctx, kid_b, info_a.pubkey, NULL, NULL,
                                 secret_ba, sizeof(secret_ba), &secret_ba_len));
    assert_true(secret_ba_len == 32);

    /* Both sides must agree */
    assert_int_equal((int)secret_ab_len, (int)secret_ba_len);
    assert_memory_equal(secret_ab, secret_ba, secret_ab_len);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid_b));
    assert_hem_ok(ctx, hem_key_delete(ctx, kid_a));
}

/* -------------------------------------------------------------------------
 * ECDH with SHA2-256 hash: output is 32 bytes regardless of curve
 * ---------------------------------------------------------------------- */
static void test_ecdh_hashed(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid_a[33] = {0}, kid_b[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "ecdh-hash-a", "CURVE25519", kid_a, sizeof(kid_a)));
    assert_hem_ok(ctx, hem_key_create(ctx, "ecdh-hash-b", "CURVE25519", kid_b, sizeof(kid_b)));

    hem_key_info_t info_b = {0};
    assert_hem_ok(ctx, hem_key_get(ctx, kid_b, &info_b));

    uint8_t secret[64] = {0};
    size_t  secret_len = 0;
    assert_hem_ok(ctx, hem_ecdh(ctx, kid_a, info_b.pubkey, NULL, "SHA2-256",
                                 secret, sizeof(secret), &secret_len));
    assert_int_equal((int)secret_len, 32);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid_b));
    assert_hem_ok(ctx, hem_key_delete(ctx, kid_a));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_ecdh_symmetric, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_ecdh_hashed,    setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
