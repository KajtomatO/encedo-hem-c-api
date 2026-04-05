/*
 * Integration tests for key management endpoints.
 * Creates and deletes temporary keys -- safe to run on any initialised device.
 *
 * Run: HEM_TEST_URL=https://my.ence.do HEM_TEST_PASS=secret ./test_keymgmt
 */
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include "../test_helpers.h"
#include "hem/hem_keymgmt.h"

/* Declared in src/internal.h; libhem exports this symbol. */
char *hem_base64_encode(const uint8_t *data, size_t len);

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/create + GET list + GET get + DELETE
 * ---------------------------------------------------------------------- */
static void test_create_list_get_delete(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* Create */
    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "test-aes", "AES256", kid, sizeof(kid)));
    assert_true(kid[0] != '\0');

    /* List -- verify our key appears */
    hem_key_info_t list[16] = {0};
    int total = 0, listed = 0;
    assert_hem_ok(ctx, hem_key_list(ctx, 0, 16, list, 16, &total, &listed));
    assert_true(listed > 0);

    int found = 0;
    for (int i = 0; i < listed; i++)
        if (strcmp(list[i].kid, kid) == 0) { found = 1; break; }
    assert_true(found);

    /* Get */
    hem_key_info_t info = {0};
    assert_hem_ok(ctx, hem_key_get(ctx, kid, &info));
    assert_true(strstr(info.type, "AES256") != NULL);

    /* Delete */
    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/update
 * ---------------------------------------------------------------------- */
static void test_update(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "before-update", "ED25519", kid, sizeof(kid)));

    assert_hem_ok(ctx, hem_key_update(ctx, kid, "after-update", NULL));

    /* Verify via list -- get doesn't return label */
    hem_key_info_t list[32] = {0};
    int total = 0, listed = 0;
    assert_hem_ok(ctx, hem_key_list(ctx, 0, 32, list, 32, &total, &listed));

    int found = 0;
    for (int i = 0; i < listed; i++)
        if (strcmp(list[i].kid, kid) == 0) {
            assert_string_equal(list[i].label, "after-update");
            found = 1;
            break;
        }
    assert_true(found);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/search
 * ---------------------------------------------------------------------- */
static void test_search(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* Create a key with a unique base64 descr (>= 6 raw bytes) */
    char kid[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "searchable", "AES256", kid, sizeof(kid)));

    /* Update with a known descr: "TESTDESCR" = 9 bytes raw, base64 = "VEVTVERFU1I=" */
    const char *descr_b64 = "VEVTVERFU1I=";
    assert_hem_ok(ctx, hem_key_update(ctx, kid, NULL, descr_b64));

    /* Search -- prefix ^ for starts-with */
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "^%s", descr_b64);

    hem_key_info_t results[8] = {0};
    int total = 0, listed = 0;
    assert_hem_ok(ctx, hem_key_search(ctx, pattern, 0, 8, results, 8, &total, &listed));
    assert_true(listed > 0);

    int found = 0;
    for (int i = 0; i < listed; i++)
        if (strcmp(results[i].kid, kid) == 0) { found = 1; break; }
    assert_true(found);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/import
 * ---------------------------------------------------------------------- */
static void test_import(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* Generate an X25519 keypair locally */
    uint8_t priv_scalar[32];
    for (int i = 0; i < 32; i++) priv_scalar[i] = (uint8_t)(i + 0xA0);

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);

    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);

    /* Base64-encode the public key */
    char *pub_b64 = hem_base64_encode(pub, 32);
    assert_non_null(pub_b64);

    /* Import */
    char kid[33] = {0};
    hem_error_t err = hem_key_import(ctx, "imported-peer", "CURVE25519",
                                     pub_b64, "ECDH", kid, sizeof(kid));
    free(pub_b64);
    assert_hem_ok(ctx, err);
    assert_true(kid[0] != '\0');

    /* Verify it appears in list */
    hem_key_info_t info = {0};
    assert_hem_ok(ctx, hem_key_get(ctx, kid, &info));
    assert_true(strstr(info.type, "CURVE25519") != NULL);

    assert_hem_ok(ctx, hem_key_delete(ctx, kid));
}

/* -------------------------------------------------------------------------
 * POST /api/keymgmt/derive
 * ---------------------------------------------------------------------- */
static void test_derive(void **state)
{
    hem_ctx_t *ctx = ((test_state_t *)*state)->ctx;

    /* Create two CURVE25519 keys */
    char kid_a[33] = {0}, kid_b[33] = {0};
    assert_hem_ok(ctx, hem_key_create(ctx, "derive-key-a", "CURVE25519", kid_a, sizeof(kid_a)));
    assert_hem_ok(ctx, hem_key_create(ctx, "derive-key-b", "CURVE25519", kid_b, sizeof(kid_b)));

    /* Get pubkey of B */
    hem_key_info_t info_b = {0};
    assert_hem_ok(ctx, hem_key_get(ctx, kid_b, &info_b));
    assert_true(info_b.pubkey[0] != '\0');

    /* Derive AES256 from A using B's pubkey */
    char kid_derived[33] = {0};
    hem_error_t err = hem_key_derive(ctx, "derived-aes", "AES256",
                                     kid_a, info_b.pubkey,
                                     kid_derived, sizeof(kid_derived));
    assert_hem_ok(ctx, err);
    assert_true(kid_derived[0] != '\0');

    /* Clean up */
    assert_hem_ok(ctx, hem_key_delete(ctx, kid_derived));
    assert_hem_ok(ctx, hem_key_delete(ctx, kid_b));
    assert_hem_ok(ctx, hem_key_delete(ctx, kid_a));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_list_get_delete, setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_update,                 setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_search,                 setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_import,                 setup_user, teardown_ctx),
        cmocka_unit_test_setup_teardown(test_derive,                 setup_user, teardown_ctx),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
