/*
 * test_ext_sim.c — simulated ExtAuth authenticator (tests/support/ext_sim.h).
 *
 * verifies: REQ-TEST-006 (shim AES-128-CBC against the NIST SP 800-38A F.2.1
 *           known answer; scheme-A codec round-trip, tamper rejection, and a
 *           hand-decrypt against the firmware construction — K = HMAC(key=
 *           ECDH, msg=jti), AES-128-CBC K[0..15]/K[16..31], pad-byte=count,
 *           trailer = HMAC(key=K, msg=unpadded); HS256 JWT build/open/peek
 *           with the byte-identical login-eJWT header)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "crypto_shim.h"
#include "ejwt.h"
#include "ext_sim.h"
#include "json.h"

/* strdup is POSIX, not C99 — local copy so strict -std=c99 stays clean. */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static size_t unhex(const char *hex, uint8_t *out)
{
    size_t n = 0;
    for (const char *p = hex; p[0] && p[1]; p += 2) {
        unsigned hi = (p[0] <= '9') ? (unsigned)(p[0] - '0')
                                    : (unsigned)((p[0] | 0x20) - 'a' + 10);
        unsigned lo = (p[1] <= '9') ? (unsigned)(p[1] - '0')
                                    : (unsigned)((p[1] | 0x20) - 'a' + 10);
        out[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

/* --- shim AES-128-CBC: NIST SP 800-38A F.2.1/F.2.2 (two blocks) ----------- */
static void test_aes_cbc_vector(void **state)
{
    (void)state;
    uint8_t key[16], iv[16], pt[32], ct[32], out[32];

    unhex("2b7e151628aed2a6abf7158809cf4f3c", key);
    unhex("000102030405060708090a0b0c0d0e0f", iv);
    unhex("6bc1bee22e409f96e93d7e117393172a"
          "ae2d8a571e03ac9c9eb76fac45af8e51", pt);
    unhex("7649abac8119b246cee98e9b12e9197d"
          "5086cb9b507219ee95db113a917678b2", ct);

    assert_int_equal(ehem_aes128_cbc_encrypt(key, iv, pt, sizeof pt, out),
                     EHEM_OK);
    assert_memory_equal(out, ct, sizeof ct);

    assert_int_equal(ehem_aes128_cbc_decrypt(key, iv, ct, sizeof ct, out),
                     EHEM_OK);
    assert_memory_equal(out, pt, sizeof pt);

    /* In-place operation (the codec decrypts in place). */
    memcpy(out, pt, sizeof pt);
    assert_int_equal(ehem_aes128_cbc_encrypt(key, iv, out, sizeof out, out),
                     EHEM_OK);
    assert_memory_equal(out, ct, sizeof ct);

    /* Argument contract. */
    assert_int_equal(ehem_aes128_cbc_encrypt(key, iv, pt, 0, out),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_aes128_cbc_encrypt(key, iv, pt, 17, out),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_aes128_cbc_encrypt(NULL, iv, pt, 16, out),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_aes128_cbc_decrypt(key, NULL, pt, 16, out),
                     EHEM_ERR_ARG);
}

/* --- keypairs: uniqueness, determinism, DH agreement ---------------------- */
static void test_keypairs(void **state)
{
    (void)state;
    ehem_sim_keypair a, b, c, seeded1, seeded2;
    uint8_t seed[32], ab[32], ba[32];

    assert_int_equal(ehem_sim_keypair_gen(&a), 0);
    assert_int_equal(ehem_sim_keypair_gen(&b), 0);
    assert_int_equal(ehem_sim_keypair_gen(&c), 0);
    assert_memory_not_equal(a.pub, b.pub, 32);   /* per-run unique */
    assert_memory_not_equal(b.pub, c.pub, 32);

    unhex("0101010101010101010101010101010101010101010101010101010101010101",
          seed);
    assert_int_equal(ehem_sim_keypair_from_seed(seed, &seeded1), 0);
    assert_int_equal(ehem_sim_keypair_from_seed(seed, &seeded2), 0);
    assert_memory_equal(seeded1.priv, seeded2.priv, 32);
    assert_memory_equal(seeded1.pub, seeded2.pub, 32);

    /* X25519 agreement through the sim wrapper. */
    assert_int_equal(ehem_sim_shared(&a, b.pub, ab), 0);
    assert_int_equal(ehem_sim_shared(&b, a.pub, ba), 0);
    assert_memory_equal(ab, ba, 32);
}

/* --- std-base64 claim helpers --------------------------------------------- */
static void test_b64_helpers(void **state)
{
    (void)state;
    uint8_t in[32], out[32];
    for (size_t i = 0; i < sizeof in; i++) {
        in[i] = (uint8_t)(i * 7 + 3);
    }

    char *b64 = ehem_sim_b64(in, sizeof in);
    assert_non_null(b64);
    assert_int_equal(strlen(b64), 44);            /* 32 bytes → 44 chars, padded */
    assert_int_equal(b64[43], '=');
    assert_int_equal(ehem_sim_b64_key(b64, out), 0);
    assert_memory_equal(in, out, 32);
    free(b64);

    /* Wrong decoded length is rejected. */
    char *short_b64 = ehem_sim_b64(in, 31);
    assert_non_null(short_b64);
    assert_int_equal(ehem_sim_b64_key(short_b64, out), -1);
    free(short_b64);
}

/* --- compact HS256 JWT ----------------------------------------------------- */
static void test_jwt_roundtrip(void **state)
{
    (void)state;
    uint8_t key[32], wrong_key[32];
    unhex("a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5",
          key);
    unhex("5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a",
          wrong_key);

    ehem_json *claims = ehem_json_new_object();
    assert_non_null(claims);
    assert_true(ehem_json_add_string(claims, "jti", "dGVzdC1qdGk="));
    assert_true(ehem_json_add_string(claims, "iss", "c2ltLXB1YmtleQ=="));
    assert_true(ehem_json_add_string(claims, "label", "EHEMTEST-sim"));
    assert_true(ehem_json_add_int64(claims, "exp", 1798000000));

    char *jwt = NULL;
    assert_int_equal(ehem_sim_jwt_build(claims, key, &jwt), 0);
    assert_non_null(jwt);
    ehem_json_free(claims);

    /* Open with the right key: claims come back. */
    ehem_json *payload = ehem_sim_jwt_open(jwt, key);
    assert_non_null(payload);
    const char *sv = NULL;
    int64_t iv64 = 0;
    assert_true(ehem_json_get_string(payload, "label", &sv));
    assert_string_equal(sv, "EHEMTEST-sim");
    assert_true(ehem_json_get_int64(payload, "exp", &iv64));
    assert_int_equal(iv64, 1798000000);
    ehem_json_free(payload);

    /* Peek needs no key. */
    payload = ehem_sim_jwt_peek(jwt);
    assert_non_null(payload);
    assert_true(ehem_json_get_string(payload, "jti", &sv));
    assert_string_equal(sv, "dGVzdC1qdGk=");
    ehem_json_free(payload);

    /* Wrong key: signature check fails. */
    assert_null(ehem_sim_jwt_open(jwt, wrong_key));

    /* Tampered payload: flip one payload character (base64url-safe swap). */
    char *dot1 = strchr(jwt, '.');
    assert_non_null(dot1);
    dot1[1] = (dot1[1] == 'A') ? 'B' : 'A';
    assert_null(ehem_sim_jwt_open(jwt, key));
    free(jwt);

    /* Structurally broken tokens. */
    assert_null(ehem_sim_jwt_open("no-dots-here", key));
    assert_null(ehem_sim_jwt_open("a.b.c.d", key));
    assert_null(ehem_sim_jwt_peek("only.one"));
}

/* The sim header must be byte-identical to the login eJWT header — the
 * firmware accepts that exact header from the working login flow. */
static void test_jwt_header_matches_ejwt(void **state)
{
    (void)state;
    uint8_t key[32] = {1};

    ehem_json *claims = ehem_json_new_object();
    assert_non_null(claims);
    assert_true(ehem_json_add_string(claims, "jti", "x"));
    char *sim_jwt = NULL;
    assert_int_equal(ehem_sim_jwt_build(claims, key, &sim_jwt), 0);
    ehem_json_free(claims);

    char *ejwt = NULL;
    uint8_t pub[32] = {2}, shared[32] = {3};
    assert_int_equal(ehem_ejwt_build("jti", "spk", "scope", pub, shared,
                                     1000, 2000, &ejwt), EHEM_OK);

    size_t sim_hlen = (size_t)(strchr(sim_jwt, '.') - sim_jwt);
    size_t ejwt_hlen = (size_t)(strchr(ejwt, '.') - ejwt);
    assert_int_equal(sim_hlen, ejwt_hlen);
    assert_memory_equal(sim_jwt, ejwt, sim_hlen);

    ehem_ejwt_free(ejwt);
    free(sim_jwt);
}

/* --- scheme A -------------------------------------------------------------- */

static void fixed_jti_ecdh(uint8_t jti[32], uint8_t ecdh[32])
{
    unhex("00112233445566778899aabbccddeeff"
          "102132435465768798a9bacbdcedfe0f", jti);
    unhex("deadbeefcafebabe0123456789abcdef"
          "fedcba9876543210deadbeefcafebabe", ecdh);
}

static void test_scheme_a_roundtrip(void **state)
{
    (void)state;
    uint8_t jti[32], ecdh[32];
    fixed_jti_ecdh(jti, ecdh);

    static const char *const scopes[] = {
        "system:config",                        /* ordinary */
        "0123456789abcdef",                     /* exactly one block → full pad block */
        "keymgmt:use:00112233445566778899aabbccddeeff00#eyJ0IjoiMDA0MSJ9", /* #-meta */
        "s",                                    /* tiny */
    };
    for (size_t i = 0; i < sizeof scopes / sizeof scopes[0]; i++) {
        char *value = NULL, *back = NULL;
        assert_int_equal(ehem_sim_scheme_a_encrypt(scopes[i], jti, ecdh,
                                                   &value), 0);
        assert_non_null(value);
        assert_int_equal(value[0], 'A');
        assert_int_equal(ehem_sim_scheme_a_decrypt(value, jti, ecdh, &back),
                         0);
        assert_string_equal(back, scopes[i]);
        free(value);
        free(back);
    }
}

/* Decrypt a codec-produced value BY HAND with shim primitives, following the
 * firmware's token-side steps literally — proves the layout matches the
 * construction the device implements, not merely that the codec inverts
 * itself. */
static void test_scheme_a_matches_firmware_construction(void **state)
{
    (void)state;
    const char *scope = "keymgmt:list";
    uint8_t jti[32], ecdh[32];
    fixed_jti_ecdh(jti, ecdh);

    char *value = NULL;
    assert_int_equal(ehem_sim_scheme_a_encrypt(scope, jti, ecdh, &value), 0);
    assert_int_equal(value[0], 'A');

    /* K = HMAC-SHA256(key=ECDH secret, msg=jti raw) — api_auth.c:1744. */
    uint8_t k[32];
    assert_int_equal(ehem_hmac_sha256(ecdh, 32, jti, 32, k), EHEM_OK);

    /* base64 body → ct || 32-byte trailer. */
    uint8_t blob[256];
    size_t blob_len = ehem_b64_std_decode(value + 1, strlen(value + 1),
                                          blob, sizeof blob);
    assert_int_not_equal(blob_len, (size_t)-1);
    size_t ct_len = blob_len - 32;
    assert_int_equal(ct_len, 16);   /* 12-char scope pads to one block */

    /* AES-128-CBC with key=K[0..15], iv=K[16..31] — api_auth.c:1762. */
    uint8_t plain[64];
    assert_int_equal(ehem_aes128_cbc_decrypt(k, k + 16, blob, ct_len, plain),
                     EHEM_OK);

    /* Pad byte = pad count (api_auth.c:1770), scope before it. */
    uint8_t pad = plain[ct_len - 1];
    assert_int_equal(pad, 16 - strlen(scope));
    assert_memory_equal(plain, scope, strlen(scope));

    /* Trailer = HMAC-SHA256(key=K, msg=unpadded scope) — api_auth.c:1782. */
    uint8_t tag[32];
    assert_int_equal(ehem_hmac_sha256(k, sizeof k, (const uint8_t *)scope,
                                      strlen(scope), tag), EHEM_OK);
    assert_memory_equal(tag, blob + ct_len, 32);

    free(value);
}

static void test_scheme_a_tamper(void **state)
{
    (void)state;
    uint8_t jti[32], ecdh[32], other_ecdh[32];
    fixed_jti_ecdh(jti, ecdh);
    memcpy(other_ecdh, ecdh, 32);
    other_ecdh[0] ^= 0xff;

    char *value = NULL, *back = NULL;
    assert_int_equal(ehem_sim_scheme_a_encrypt("system:config", jti, ecdh,
                                               &value), 0);

    /* Wrong ECDH secret (wrong K): trailer mismatch. */
    assert_int_equal(ehem_sim_scheme_a_decrypt(value, jti, other_ecdh, &back),
                     -1);

    /* Flipped ciphertext character (valid base64, different bytes). */
    char *tampered = dup_str(value);
    assert_non_null(tampered);
    tampered[2] = (tampered[2] == 'B') ? 'C' : 'B';
    assert_int_equal(ehem_sim_scheme_a_decrypt(tampered, jti, ecdh, &back),
                     -1);
    free(tampered);

    /* Missing scheme prefix / unknown scheme. */
    tampered = dup_str(value);
    assert_non_null(tampered);
    tampered[0] = 'B';
    assert_int_equal(ehem_sim_scheme_a_decrypt(tampered, jti, ecdh, &back),
                     -1);
    free(tampered);

    /* Truncated body (drops below minimum ct+trailer size). */
    tampered = dup_str(value);
    assert_non_null(tampered);
    tampered[1 + 8] = '\0';
    assert_int_equal(ehem_sim_scheme_a_decrypt(tampered, jti, ecdh, &back),
                     -1);
    free(tampered);

    assert_null(back);   /* no tamper path may have produced output */
    free(value);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_aes_cbc_vector),
        cmocka_unit_test(test_keypairs),
        cmocka_unit_test(test_b64_helpers),
        cmocka_unit_test(test_jwt_roundtrip),
        cmocka_unit_test(test_jwt_header_matches_ejwt),
        cmocka_unit_test(test_scheme_a_roundtrip),
        cmocka_unit_test(test_scheme_a_matches_firmware_construction),
        cmocka_unit_test(test_scheme_a_tamper),
    };
    /* wolfCrypt process-global init — required on Windows (RNG mutex). */
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
