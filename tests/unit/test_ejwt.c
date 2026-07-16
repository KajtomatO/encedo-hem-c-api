/*
 * test_ejwt.c — base64 codecs + eJWT builder (src/ejwt.h).
 *
 * verifies: REQ-AUTH-001 (base64/base64url round-trip + rejection; the eJWT
 *           reproduces the python-client fixture byte-for-byte)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ejwt.h"
#include "crypto_shim.h"
#include "fixtures/ejwt_login_vector.h"

#define NPOS ((size_t)-1)

/* --- base64 round-trip over all remainder classes -------------------------- */
static void test_base64_roundtrip(void **state)
{
    (void)state;
    /* 0..7 bytes covers in_len % 3 == 0,1,2 across one and two blocks. */
    uint8_t data[7];
    for (size_t i = 0; i < sizeof data; i++) {
        data[i] = (uint8_t)(0xF0 ^ (i * 37));
    }

    for (size_t n = 0; n <= sizeof data; n++) {
        char enc[32];
        uint8_t dec[16];

        size_t sn = ehem_b64_std_encode(data, n, enc, sizeof enc);
        assert_int_equal(sn, ehem_b64_std_encoded_len(n));
        assert_int_equal(strlen(enc), sn);
        assert_int_equal(ehem_b64_std_decode(enc, sn, dec, sizeof dec), n);
        assert_memory_equal(dec, data, n);

        size_t un = ehem_b64url_encode(data, n, enc, sizeof enc);
        assert_int_equal(un, ehem_b64url_encoded_len(n));
        assert_int_equal(strlen(enc), un);
        assert_int_equal(ehem_b64url_decode(enc, un, dec, sizeof dec), n);
        assert_memory_equal(dec, data, n);
    }
}

/* --- known RFC 4648 vectors ------------------------------------------------ */
static void test_base64_known_vectors(void **state)
{
    (void)state;
    char enc[16];

    assert_int_equal(ehem_b64_std_encode((const uint8_t *)"f",   1, enc, sizeof enc), 4);
    assert_string_equal(enc, "Zg==");
    assert_int_equal(ehem_b64_std_encode((const uint8_t *)"fo",  2, enc, sizeof enc), 4);
    assert_string_equal(enc, "Zm8=");
    assert_int_equal(ehem_b64_std_encode((const uint8_t *)"foobar", 6, enc, sizeof enc), 8);
    assert_string_equal(enc, "Zm9vYmFy");

    assert_int_equal(ehem_b64url_encode((const uint8_t *)"f",  1, enc, sizeof enc), 2);
    assert_string_equal(enc, "Zg");
    assert_int_equal(ehem_b64url_encode((const uint8_t *)"fo", 2, enc, sizeof enc), 3);
    assert_string_equal(enc, "Zm8");

    /* All-ones → the 63-index char differs per alphabet ('/' vs '_'). */
    uint8_t ones[3] = {0xFF, 0xFF, 0xFF};
    assert_int_equal(ehem_b64_std_encode(ones, 3, enc, sizeof enc), 4);
    assert_string_equal(enc, "////");
    assert_int_equal(ehem_b64url_encode(ones, 3, enc, sizeof enc), 4);
    assert_string_equal(enc, "____");
}

/* --- rejection: wrong-alphabet chars, impossible length, tiny buffers ------- */
static void test_base64_reject(void **state)
{
    (void)state;
    uint8_t out[16];
    char enc[8];

    /* std must reject the base64url-only chars, and vice versa. */
    assert_int_equal(ehem_b64_std_decode("____", 4, out, sizeof out), NPOS);
    assert_int_equal(ehem_b64_std_decode("-AAA", 4, out, sizeof out), NPOS);
    assert_int_equal(ehem_b64url_decode("////", 4, out, sizeof out), NPOS);
    assert_int_equal(ehem_b64url_decode("+AAA", 4, out, sizeof out), NPOS);

    /* garbage / whitespace / impossible one-char length. */
    assert_int_equal(ehem_b64_std_decode("ab!d", 4, out, sizeof out), NPOS);
    assert_int_equal(ehem_b64url_decode("Z",    1, out, sizeof out), NPOS);

    /* but each accepts its own 63-index char. */
    assert_int_equal(ehem_b64_std_decode("////", 4, out, sizeof out), 3);
    assert_int_equal(ehem_b64url_decode("____", 4, out, sizeof out), 3);

    /* insufficient output capacity → error, not overflow. */
    assert_int_equal(ehem_b64_std_encode((const uint8_t *)"foobar", 6, enc, 4), NPOS);
    assert_int_equal(ehem_b64_std_decode("Zm9vYmFy", 8, out, 2), NPOS);
}

/* Decode the payload segment of an eJWT into a NUL-terminated JSON string. */
static void decode_payload(const char *ejwt, char *buf, size_t buf_cap)
{
    const char *d1 = strchr(ejwt, '.');
    assert_non_null(d1);
    const char *d2 = strchr(d1 + 1, '.');
    assert_non_null(d2);
    uint8_t raw[256];
    size_t n = ehem_b64url_decode(d1 + 1, (size_t)(d2 - (d1 + 1)), raw, sizeof raw);
    assert_int_not_equal(n, NPOS);
    assert_true(n < buf_cap);
    memcpy(buf, raw, n);
    buf[n] = '\0';
}

/* --- the golden fixture: full derive → build must match the python client --- */
static void test_ejwt_matches_python_fixture(void **state)
{
    (void)state;

    uint8_t seed[32], priv[32], pub[32], spk_raw[64], shared[32];

    assert_int_equal(ehem_kdf_pbkdf2_sha256(
        (const uint8_t *)EJWT_FX_PASSPHRASE, strlen(EJWT_FX_PASSPHRASE),
        (const uint8_t *)EJWT_FX_EID, strlen(EJWT_FX_EID),
        600000, seed, sizeof seed), EHEM_OK);

    assert_int_equal(ehem_x25519_keypair_from_seed(seed, priv, pub), EHEM_OK);

    /* iss = std base64 of the derived public key. */
    char iss[64];
    assert_int_not_equal(ehem_b64_std_encode(pub, 32, iss, sizeof iss), NPOS);
    assert_string_equal(iss, EJWT_FX_EXPECT_ISS);

    size_t spk_n = ehem_b64_std_decode(EJWT_FX_SPK, strlen(EJWT_FX_SPK),
                                       spk_raw, sizeof spk_raw);
    assert_int_equal(spk_n, 32);
    assert_int_equal(ehem_x25519_shared(priv, spk_raw, shared), EHEM_OK);

    /* requested_exp < the fixture's challenge deadline, so removing the old
     * min(requested, challenge) cap leaves this vector byte-identical. */
    char *ejwt = NULL;
    assert_int_equal(ehem_ejwt_build(EJWT_FX_JTI, EJWT_FX_SPK,
                                     EJWT_FX_SCOPE, pub, shared,
                                     EJWT_FX_NOW, EJWT_FX_REQUESTED_EXP, &ejwt),
                     EHEM_OK);
    assert_non_null(ejwt);
    assert_string_equal(ejwt, EJWT_FX_EXPECT_EJWT);

    ehem_ejwt_free(ejwt);
    ehem_zeroize(seed, sizeof seed);
    ehem_zeroize(priv, sizeof priv);
    ehem_zeroize(shared, sizeof shared);
}

/* --- exp claim (verbatim, uncapped) + arg validation (crypto-independent) --- */
static void test_ejwt_exp_and_args(void **state)
{
    (void)state;
    uint8_t pub[32] = {0}, shared[32] = {0};
    char *ejwt = NULL;
    char payload[300];

    /* The exp claim is the requested value verbatim — NOT capped at any
     * challenge deadline (STEP-M2-045). Even a far-future request is emitted
     * as-is. */
    assert_int_equal(ehem_ejwt_build("jti", "aud", "sc",
                                     pub, shared, 1700000000, 2000000999, &ejwt),
                     EHEM_OK);
    decode_payload(ejwt, payload, sizeof payload);
    assert_non_null(strstr(payload, "\"exp\":2000000999"));
    assert_non_null(strstr(payload, "\"iat\":1700000000"));
    ehem_ejwt_free(ejwt);

    ejwt = NULL;
    assert_int_equal(ehem_ejwt_build("jti", "aud", "sc",
                                     pub, shared, 1700000000, 1700003600, &ejwt),
                     EHEM_OK);
    decode_payload(ejwt, payload, sizeof payload);
    assert_non_null(strstr(payload, "\"exp\":1700003600"));
    ehem_ejwt_free(ejwt);

    /* NULL arguments rejected. */
    ejwt = (char *)0x1;
    assert_int_equal(ehem_ejwt_build(NULL, "a", "s", pub, shared, 1, 1, &ejwt),
                     EHEM_ERR_ARG);
    assert_null(ejwt);
    assert_int_equal(ehem_ejwt_build("j", "a", "s", NULL, shared, 1, 1, &ejwt),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ejwt_build("j", "a", "s", pub, shared, 1, 1, NULL),
                     EHEM_ERR_ARG);

    ehem_ejwt_free(NULL);  /* NULL-safe */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_base64_roundtrip),
        cmocka_unit_test(test_base64_known_vectors),
        cmocka_unit_test(test_base64_reject),
        cmocka_unit_test(test_ejwt_matches_python_fixture),
        cmocka_unit_test(test_ejwt_exp_and_args),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
