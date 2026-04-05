/*
 * Unit tests for hem_build_ejwt (eJWT construction).
 *
 * Verifies structure and correctness with known test vectors without
 * requiring a live device.
 *
 * Test vector derivation:
 *   passphrase = "test-pass"
 *   eid        = "test-eid"
 *   spk        = X25519 public key from fixed private scalar 0x02 * 32
 *              = (base64 of the computed public key -- verified below)
 *   jti        = "test-nonce-1234"
 *   exp        = 9999999999  (far future)
 *   scope      = "system:status"
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cmocka.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * Helper: base64url decode (strips padding, converts - and _ back)
 * Returns malloc'd buffer; caller must free().
 * ---------------------------------------------------------------------- */
static uint8_t *b64url_decode(const char *in, size_t *out_len)
{
    if (!in) return NULL;

    size_t in_len = strlen(in);
    /* Re-add padding */
    char *padded = malloc(in_len + 4);
    if (!padded) return NULL;
    memcpy(padded, in, in_len);
    size_t padded_len = in_len;

    /* Convert base64url -> base64 */
    for (size_t i = 0; i < padded_len; i++) {
        if (padded[i] == '-') padded[i] = '+';
        if (padded[i] == '_') padded[i] = '/';
    }
    /* Add padding */
    while (padded_len % 4 != 0) padded[padded_len++] = '=';
    padded[padded_len] = '\0';

    uint8_t *result = hem_base64_decode(padded, out_len);
    free(padded);
    return result;
}

/* -------------------------------------------------------------------------
 * Helper: split "a.b.c" into three parts (modifies the string in place).
 * ---------------------------------------------------------------------- */
static int split_jwt(char *jwt, char **h, char **p, char **s)
{
    *h = jwt;
    *p = strchr(jwt, '.');
    if (!*p) return -1;
    **p = '\0';
    (*p)++;

    *s = strchr(*p, '.');
    if (!*s) return -1;
    **s = '\0';
    (*s)++;

    return 0;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static void test_ejwt_returns_ok(void **state)
{
    (void)state;

    /*
     * We need a valid X25519 public key for spk_b64.
     * Use public key from private scalar all-0x02 bytes.
     */
    uint8_t priv_scalar[32];
    memset(priv_scalar, 0x02, 32);

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);

    uint8_t pub[32];
    size_t pub_len = 32;
    int rc = EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);
    assert_int_equal(rc, 1);

    char *spk_b64 = hem_base64_encode(pub, 32);
    assert_non_null(spk_b64);

    char ejwt[2048] = {0};
    hem_error_t err = hem_build_ejwt(
        "test-pass",
        "test-eid",
        spk_b64,
        "test-nonce-1234",
        9999999999LL,
        "system:status",
        ejwt,
        sizeof(ejwt));
    free(spk_b64);

    assert_int_equal(err, HEM_OK);
    assert_true(strlen(ejwt) > 10);
}

static void test_ejwt_has_three_parts(void **state)
{
    (void)state;

    uint8_t priv_scalar[32];
    memset(priv_scalar, 0x03, 32);
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);
    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);
    char *spk_b64 = hem_base64_encode(pub, 32);

    char ejwt[2048] = {0};
    hem_error_t err = hem_build_ejwt("pass", "eid", spk_b64,
                                     "jti", 9999999999LL, "scope",
                                     ejwt, sizeof(ejwt));
    free(spk_b64);
    assert_int_equal(err, HEM_OK);

    /* Count dots -- must be exactly 2 */
    int dots = 0;
    for (const char *p = ejwt; *p; p++)
        if (*p == '.') dots++;
    assert_int_equal(dots, 2);
}

static void test_ejwt_header_fields(void **state)
{
    (void)state;

    uint8_t priv_scalar[32];
    memset(priv_scalar, 0x04, 32);
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);
    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);
    char *spk_b64 = hem_base64_encode(pub, 32);

    char ejwt[2048] = {0};
    hem_build_ejwt("pass", "eid", spk_b64, "jti", 9999999999LL,
                   "scope", ejwt, sizeof(ejwt));
    free(spk_b64);

    char copy[2048];
    strncpy(copy, ejwt, sizeof(copy) - 1);
    char *h, *p, *s;
    assert_int_equal(split_jwt(copy, &h, &p, &s), 0);

    /* Decode and parse header */
    size_t hdr_len = 0;
    uint8_t *hdr_raw = b64url_decode(h, &hdr_len);
    assert_non_null(hdr_raw);

    cJSON *hdr = cJSON_ParseWithLength((char *)hdr_raw, hdr_len);
    free(hdr_raw);
    assert_non_null(hdr);

    char buf[64] = {0};
    hem_json_get_str(hdr, "alg",  buf, sizeof(buf)); assert_string_equal(buf, "HS256");
    hem_json_get_str(hdr, "typ",  buf, sizeof(buf)); assert_string_equal(buf, "JWT");
    hem_json_get_str(hdr, "ecdh", buf, sizeof(buf)); assert_string_equal(buf, "x25519");

    cJSON_Delete(hdr);
}

static void test_ejwt_payload_fields(void **state)
{
    (void)state;

    uint8_t priv_scalar[32];
    memset(priv_scalar, 0x05, 32);
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);
    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);
    char *spk_b64 = hem_base64_encode(pub, 32);

    char ejwt[2048] = {0};
    hem_build_ejwt("test-pass", "test-eid", spk_b64,
                   "my-jti", 9999999999LL, "keymgmt:gen",
                   ejwt, sizeof(ejwt));
    free(spk_b64);

    char copy[2048];
    strncpy(copy, ejwt, sizeof(copy) - 1);
    char *h, *p, *s;
    split_jwt(copy, &h, &p, &s);

    size_t pay_len = 0;
    uint8_t *pay_raw = b64url_decode(p, &pay_len);
    assert_non_null(pay_raw);

    cJSON *pay = cJSON_ParseWithLength((char *)pay_raw, pay_len);
    free(pay_raw);
    assert_non_null(pay);

    char buf[256] = {0};
    hem_json_get_str(pay, "jti",   buf, sizeof(buf)); assert_string_equal(buf, "my-jti");
    hem_json_get_str(pay, "scope", buf, sizeof(buf)); assert_string_equal(buf, "keymgmt:gen");
    int64_t exp = hem_json_get_int64(pay, "exp", 0);
    assert_int_equal((int)exp, (int)9999999999LL);

    cJSON_Delete(pay);
}

static void test_ejwt_signature_verifies(void **state)
{
    (void)state;

    /*
     * Derive the expected HMAC independently and compare with the
     * signature part of the eJWT.
     *
     * key_bytes = private scalar 0x06 * 32
     * passphrase = "verify-pass", eid = "my-eid"
     * spk = X25519_pub(0x06 * 32) encoded in base64
     *
     * Algorithm:
     *   seed   = PBKDF2-SHA256("verify-pass", "my-eid", 600000, 32)
     *   shared = X25519(seed, spk_raw)
     *   sig    = HMAC-SHA256(shared, b64url(hdr) + "." + b64url(pay))
     */
    const char *passphrase = "verify-pass";
    const char *eid        = "my-eid";

    /* Build device spk from a known scalar */
    uint8_t dev_priv[32];
    memset(dev_priv, 0x06, 32);
    EVP_PKEY *dev_pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                        dev_priv, 32);
    assert_non_null(dev_pkey);
    uint8_t dev_pub[32]; size_t dev_pub_len = 32;
    EVP_PKEY_get_raw_public_key(dev_pkey, dev_pub, &dev_pub_len);
    EVP_PKEY_free(dev_pkey);

    char *spk_b64 = hem_base64_encode(dev_pub, 32);
    assert_non_null(spk_b64);

    char ejwt[2048] = {0};
    hem_error_t err = hem_build_ejwt(passphrase, eid, spk_b64,
                                     "nonce", 9999999999LL, "system:status",
                                     ejwt, sizeof(ejwt));
    assert_int_equal(err, HEM_OK);

    /* Split the JWT */
    char copy[2048];
    strncpy(copy, ejwt, sizeof(copy) - 1);
    char *h, *p, *s;
    assert_int_equal(split_jwt(copy, &h, &p, &s), 0);

    /* Re-derive shared secret the same way hem_build_ejwt does */
    uint8_t seed[32] = {0};
    assert_int_equal(
        PKCS5_PBKDF2_HMAC(passphrase, (int)strlen(passphrase),
                           (unsigned char *)eid, (int)strlen(eid),
                           600000, EVP_sha256(), 32, seed),
        1);

    EVP_PKEY *usr_pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                        seed, 32);
    assert_non_null(usr_pkey);

    EVP_PKEY *peer_pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL,
                                                       dev_pub, 32);
    assert_non_null(peer_pkey);

    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new(usr_pkey, NULL);
    assert_non_null(kctx);
    assert_int_equal(EVP_PKEY_derive_init(kctx), 1);
    assert_int_equal(EVP_PKEY_derive_set_peer(kctx, peer_pkey), 1);
    uint8_t shared[32]; size_t shared_len = 32;
    assert_int_equal(EVP_PKEY_derive(kctx, shared, &shared_len), 1);
    EVP_PKEY_CTX_free(kctx);
    EVP_PKEY_free(usr_pkey);
    EVP_PKEY_free(peer_pkey);

    /* signing_input = h + "." + p (the two base64url-encoded parts) */
    size_t h_len = strlen(h);
    size_t p_len = strlen(p);
    char *signing = malloc(h_len + 1 + p_len + 1);
    assert_non_null(signing);
    memcpy(signing, h, h_len);
    signing[h_len] = '.';
    memcpy(signing + h_len + 1, p, p_len);
    signing[h_len + 1 + p_len] = '\0';

    /* Compute expected HMAC-SHA256 */
    uint8_t expected_sig[32]; unsigned int expected_sig_len = 32;
    assert_non_null(HMAC(EVP_sha256(), shared, (int)shared_len,
                         (unsigned char *)signing, h_len + 1 + p_len,
                         expected_sig, &expected_sig_len));
    free(signing);

    /* Decode the signature from the JWT */
    size_t sig_len = 0;
    uint8_t *actual_sig = b64url_decode(s, &sig_len);
    assert_non_null(actual_sig);
    assert_int_equal((int)sig_len, 32);
    assert_memory_equal(actual_sig, expected_sig, 32);
    free(actual_sig);
    free(spk_b64);
}

static void test_ejwt_buffer_too_small(void **state)
{
    (void)state;

    uint8_t priv_scalar[32];
    memset(priv_scalar, 0x07, 32);
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                    priv_scalar, 32);
    assert_non_null(pkey);
    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    EVP_PKEY_free(pkey);
    char *spk_b64 = hem_base64_encode(pub, 32);

    char tiny[10] = {0};
    hem_error_t err = hem_build_ejwt("pass", "eid", spk_b64, "jti",
                                     9999999999LL, "scope",
                                     tiny, sizeof(tiny));
    free(spk_b64);
    /* snprintf truncation is treated as an error inside hem_build_ejwt */
    assert_int_not_equal(err, HEM_OK);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ejwt_returns_ok),
        cmocka_unit_test(test_ejwt_has_three_parts),
        cmocka_unit_test(test_ejwt_header_fields),
        cmocka_unit_test(test_ejwt_payload_fields),
        cmocka_unit_test(test_ejwt_signature_verifies),
        cmocka_unit_test(test_ejwt_buffer_too_small),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
