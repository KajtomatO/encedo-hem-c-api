/*
 * test_keytype.c — the key-type classifier (src/keytype.c), pure/offline.
 *
 * verifies: REQ-KEY-006 (every documented bare algorithm name and the four
 *           live-observed flag-set strings classify to the correct family,
 *           modes, and roles; unknown tokens are skipped without error; size
 *           and format metadata matches the wire reality — compressed-x963
 *           NIST pubkeys with DER signatures, raw Ed25519/Ed448 and ML-DSA
 *           sizes per RFC 8032 / FIPS 203-204)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/keymgmt.h"

/* Parse `str` and assert the full info quintuple. */
static void expect_info(const char *str, ehem_key_family family,
                        size_t pubkey_len, size_t sig_max_len, int sig_der)
{
    ehem_key_type_info info;
    assert_int_equal(ehem_key_type_parse(str, &info), EHEM_OK);
    assert_int_equal(info.family, family);
    assert_int_equal(info.pubkey_len, pubkey_len);
    assert_int_equal(info.sig_max_len, sig_max_len);
    assert_int_equal(info.sig_der, sig_der);
}

/* --- every documented bare name (create.md + get.md vocabulary) ------------ */
static void test_bare_names(void **state)
{
    (void)state;
    expect_info("SECP256R1", EHEM_KEY_FAMILY_SECP256R1, 33, 72, 1);
    expect_info("SECP384R1", EHEM_KEY_FAMILY_SECP384R1, 49, 104, 1);
    expect_info("SECP521R1", EHEM_KEY_FAMILY_SECP521R1, 67, 141, 1);
    expect_info("SECP256K1", EHEM_KEY_FAMILY_SECP256K1, 33, 72, 1);
    expect_info("ED25519", EHEM_KEY_FAMILY_ED25519, 32, 64, 0);
    expect_info("ED448", EHEM_KEY_FAMILY_ED448, 57, 114, 0);
    expect_info("CURVE25519", EHEM_KEY_FAMILY_CURVE25519, 32, 0, 0);
    expect_info("CURVE448", EHEM_KEY_FAMILY_CURVE448, 56, 0, 0);
    expect_info("AES128", EHEM_KEY_FAMILY_AES128, 0, 0, 0);
    expect_info("AES192", EHEM_KEY_FAMILY_AES192, 0, 0, 0);
    expect_info("AES256", EHEM_KEY_FAMILY_AES256, 0, 0, 0);
    expect_info("SHA2-256", EHEM_KEY_FAMILY_HMAC_SHA2_256, 0, 0, 0);
    expect_info("SHA2-384", EHEM_KEY_FAMILY_HMAC_SHA2_384, 0, 0, 0);
    expect_info("SHA2-512", EHEM_KEY_FAMILY_HMAC_SHA2_512, 0, 0, 0);
    expect_info("SHA3-256", EHEM_KEY_FAMILY_HMAC_SHA3_256, 0, 0, 0);
    expect_info("SHA3-384", EHEM_KEY_FAMILY_HMAC_SHA3_384, 0, 0, 0);
    expect_info("SHA3-512", EHEM_KEY_FAMILY_HMAC_SHA3_512, 0, 0, 0);
    expect_info("MLKEM512", EHEM_KEY_FAMILY_MLKEM512, 800, 0, 0);
    expect_info("MLKEM768", EHEM_KEY_FAMILY_MLKEM768, 1184, 0, 0);
    expect_info("MLKEM1024", EHEM_KEY_FAMILY_MLKEM1024, 1568, 0, 0);
    expect_info("MLDSA44", EHEM_KEY_FAMILY_MLDSA44, 1312, 2420, 0);
    expect_info("MLDSA65", EHEM_KEY_FAMILY_MLDSA65, 1952, 3309, 0);
    expect_info("MLDSA87", EHEM_KEY_FAMILY_MLDSA87, 2592, 4627, 0);
    expect_info("CERT", EHEM_KEY_FAMILY_CERT, 0, 0, 0);
    expect_info("DER_PKEY", EHEM_KEY_FAMILY_DER_PKEY, 0, 0, 0);
}

/* --- the four flag-set strings observed live (my.ence.do, fw v1.2.2) ------- */
static void test_live_flag_sets(void **state)
{
    (void)state;
    ehem_key_type_info info;

    assert_int_equal(ehem_key_type_parse("ATT,PKEY,ECDH,ExDSA,SECP256R1",
                                         &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_SECP256R1);
    assert_int_equal(info.modes, EHEM_KEY_MODE_ECDH | EHEM_KEY_MODE_EXDSA);
    assert_int_equal(info.roles, EHEM_KEY_ROLE_ATT | EHEM_KEY_ROLE_PKEY);
    assert_int_equal(info.pubkey_len, 33);
    assert_int_equal(info.sig_max_len, 72);
    assert_true(info.sig_der);

    assert_int_equal(ehem_key_type_parse("PKEY,GENERIC_DER", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_DER_PKEY);
    assert_int_equal(info.modes, 0);
    assert_int_equal(info.roles,
                     EHEM_KEY_ROLE_PKEY | EHEM_KEY_ROLE_GENERIC_DER);

    assert_int_equal(ehem_key_type_parse("CERT,GENERIC_DER", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_CERT);
    assert_int_equal(info.roles,
                     EHEM_KEY_ROLE_CERT | EHEM_KEY_ROLE_GENERIC_DER);

    assert_int_equal(ehem_key_type_parse("ECDH,CURVE25519", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_CURVE25519);
    assert_int_equal(info.modes, EHEM_KEY_MODE_ECDH);
    assert_int_equal(info.roles, 0);
    assert_int_equal(info.pubkey_len, 32);
    assert_int_equal(info.sig_max_len, 0);
}

/* --- tolerance: unknown tokens skipped, order never matters ---------------- */
static void test_tolerant_parsing(void **state)
{
    (void)state;
    ehem_key_type_info info;

    /* A token the firmware might add later must not disturb the known ones. */
    assert_int_equal(ehem_key_type_parse("NEWFLAG,ED25519", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_ED25519);
    assert_int_equal(info.modes, 0);
    assert_int_equal(info.roles, 0);
    assert_int_equal(info.sig_max_len, 64);

    /* No recognizable algorithm: family unknown, still EHEM_OK. */
    assert_int_equal(ehem_key_type_parse("FOO,BAR", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_UNKNOWN);
    assert_int_equal(info.pubkey_len, 0);
    assert_int_equal(info.sig_max_len, 0);

    /* Empty string and stray commas are harmless. */
    assert_int_equal(ehem_key_type_parse("", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_UNKNOWN);
    assert_int_equal(ehem_key_type_parse(",ED25519,", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_ED25519);

    /* Token order must not change DER-container resolution. */
    assert_int_equal(ehem_key_type_parse("GENERIC_DER,CERT", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_CERT);
    assert_int_equal(ehem_key_type_parse("GENERIC_DER,PKEY", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_DER_PKEY);

    /* A prefix of a known token is NOT that token. */
    assert_int_equal(ehem_key_type_parse("SECP256", &info), EHEM_OK);
    assert_int_equal(info.family, EHEM_KEY_FAMILY_UNKNOWN);

    /* NULL arguments are the only error. */
    assert_int_equal(ehem_key_type_parse(NULL, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_type_parse("ED25519", NULL), EHEM_ERR_ARG);
}

/* --- display names ---------------------------------------------------------- */
static void test_family_str(void **state)
{
    (void)state;
    assert_string_equal(ehem_key_family_str(EHEM_KEY_FAMILY_SECP256R1),
                        "SECP256R1");
    assert_string_equal(ehem_key_family_str(EHEM_KEY_FAMILY_ED25519),
                        "ED25519");
    assert_string_equal(ehem_key_family_str(EHEM_KEY_FAMILY_HMAC_SHA2_256),
                        "SHA2-256");
    assert_string_equal(ehem_key_family_str(EHEM_KEY_FAMILY_CERT), "CERT");
    assert_string_equal(ehem_key_family_str(EHEM_KEY_FAMILY_UNKNOWN),
                        "unknown");
    assert_string_equal(ehem_key_family_str((ehem_key_family)999), "unknown");

    /* Round-trip: every family's display name parses back to itself. */
    for (int f = EHEM_KEY_FAMILY_SECP256R1; f <= EHEM_KEY_FAMILY_DER_PKEY;
         f++) {
        ehem_key_type_info info;
        assert_int_equal(
            ehem_key_type_parse(ehem_key_family_str((ehem_key_family)f),
                                &info), EHEM_OK);
        assert_int_equal(info.family, f);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bare_names),
        cmocka_unit_test(test_live_flag_sets),
        cmocka_unit_test(test_tolerant_parsing),
        cmocka_unit_test(test_family_str),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
