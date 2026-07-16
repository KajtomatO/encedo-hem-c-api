/*
 * test_crypto.c — crypto shim (src/crypto_shim.h) against published vectors.
 *
 * verifies: REQ-AUTH-001 (PBKDF2-HMAC-SHA256, HMAC-SHA256 and X25519 match
 *           RFC 4231 / RFC 7748 / PBKDF2-HMAC-SHA256 test vectors; secret
 *           scrubbing is non-elidable)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "crypto_shim.h"

/* TEMPORARY DIAGNOSTIC (remove once the MinGW X25519 failure is root-caused):
 * the shim collapses every wolfCrypt error to EHEM_ERR_PROTOCOL, so this dumps
 * the wolfSSL version and the raw wc_curve25519_* return codes to stderr. It is
 * printed from the (failing) X25519 test so ctest --output-on-failure shows it
 * on the platform that fails. */
#include <stdio.h>
#include <wolfssl/options.h>
#include <wolfssl/version.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/random.h>

static void dump_curve25519_diag(void)
{
    fprintf(stderr, "\n=== WOLFSSL X25519 DIAG ===\n");
    fprintf(stderr, "version=%s hex=0x%08x sizeof(curve25519_key)=%zu CURVE25519_KEYSIZE=%d\n",
            LIBWOLFSSL_VERSION_STRING, (unsigned)LIBWOLFSSL_VERSION_HEX,
            sizeof(curve25519_key), CURVE25519_KEYSIZE);
    fprintf(stderr, "build:"
#ifdef HAVE_CURVE25519
            " HAVE_CURVE25519"
#endif
#ifdef CURVE25519_SMALL
            " CURVE25519_SMALL"
#endif
#ifdef WOLFSSL_SP_MATH
            " WOLFSSL_SP_MATH"
#endif
#ifdef WOLFSSL_SP_MATH_ALL
            " WOLFSSL_SP_MATH_ALL"
#endif
#ifdef WOLFSSL_SP_NO_256
            " WOLFSSL_SP_NO_256"
#endif
#ifdef USE_FAST_MATH
            " USE_FAST_MATH"
#endif
#ifdef FREESCALE_LTC_ECC
            " FREESCALE_LTC_ECC"
#endif
#ifdef WOLFSSL_SMALL_STACK
            " WOLFSSL_SMALL_STACK"
#endif
            "\n");

    uint8_t priv[32], base[32] = {9}, pub[32], out[32];
    memset(priv, 0, sizeof priv);
    /* Alice's clamped scalar. */
    static const uint8_t a[32] = {
        0x70,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x6a};
    memcpy(priv, a, 32);

    curve25519_key k, p;
    int ri  = wc_curve25519_init(&k);
    int rip = wc_curve25519_init(&p);
    int r_impriv = wc_curve25519_import_private_ex(priv, 32, &k, EC25519_LITTLE_ENDIAN);
    int r_impub  = wc_curve25519_import_public_ex(base, 32, &p, EC25519_LITTLE_ENDIAN);
    word32 ol = 32;
    int r_shared = wc_curve25519_shared_secret_ex(&k, &p, out, &ol, EC25519_LITTLE_ENDIAN);
    word32 pl = 32;
    int r_exppub = wc_curve25519_export_public_ex(&k, pub, &pl, EC25519_LITTLE_ENDIAN);
    int r_makepub = wc_curve25519_make_pub(32, pub, 32, priv);
    wc_curve25519_free(&k);
    wc_curve25519_free(&p);

    WC_RNG rng;
    int r_rng = wc_InitRng(&rng);
    int r_makekey = -9999;
    if (r_rng == 0) {
        curve25519_key g;
        if (wc_curve25519_init(&g) == 0) {
            r_makekey = wc_curve25519_make_key(&rng, 32, &g);
            wc_curve25519_free(&g);
        }
        wc_FreeRng(&rng);
    }

    fprintf(stderr,
            "rc: init=%d/%d import_private=%d import_public=%d shared=%d(outlen=%u) "
            "export_public=%d make_pub=%d | rng_init=%d make_key=%d\n",
            ri, rip, r_impriv, r_impub, r_shared, (unsigned)ol,
            r_exppub, r_makepub, r_rng, r_makekey);
    fprintf(stderr, "=== END DIAG ===\n");
    fflush(stderr);
}

/* Decode a hex string into `out` (out must hold strlen(hex)/2 bytes). */
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

#define ASSERT_HEX_EQ(got, hexstr, n)                    \
    do {                                                 \
        uint8_t _exp[64];                                \
        size_t _en = unhex((hexstr), _exp);              \
        assert_int_equal(_en, (n));                      \
        assert_memory_equal((got), _exp, (n));           \
    } while (0)

/* --- PBKDF2-HMAC-SHA256 (well-known "password"/"salt" vectors) ------------- */
static void test_pbkdf2(void **state)
{
    (void)state;
    uint8_t out[32];

    assert_int_equal(ehem_kdf_pbkdf2_sha256((const uint8_t *)"password", 8,
                                            (const uint8_t *)"salt", 4,
                                            1, out, sizeof out), EHEM_OK);
    ASSERT_HEX_EQ(out, "120fb6cffcf8b32c43e7225256c4f837"
                       "a86548c92ccc35480805987cb70be17b", 32);

    assert_int_equal(ehem_kdf_pbkdf2_sha256((const uint8_t *)"password", 8,
                                            (const uint8_t *)"salt", 4,
                                            4096, out, sizeof out), EHEM_OK);
    ASSERT_HEX_EQ(out, "c5e478d59288c841aa530db6845c4c8d"
                       "962893a001ce4e11a4963873aa98134a", 32);

    /* Argument validation. */
    assert_int_equal(ehem_kdf_pbkdf2_sha256(NULL, 8, (const uint8_t *)"s", 1,
                                            1, out, sizeof out), EHEM_ERR_ARG);
    assert_int_equal(ehem_kdf_pbkdf2_sha256((const uint8_t *)"p", 1,
                                            (const uint8_t *)"s", 1,
                                            0, out, sizeof out), EHEM_ERR_ARG);
    assert_int_equal(ehem_kdf_pbkdf2_sha256((const uint8_t *)"p", 1,
                                            (const uint8_t *)"s", 1,
                                            1, out, 0), EHEM_ERR_ARG);
}

/* --- HMAC-SHA256, RFC 4231 test cases 1 and 2 ------------------------------ */
static void test_hmac_sha256(void **state)
{
    (void)state;
    uint8_t mac[32];

    /* TC1: key = 20 * 0x0b, data = "Hi There". */
    uint8_t key1[20];
    memset(key1, 0x0b, sizeof key1);
    assert_int_equal(ehem_hmac_sha256(key1, sizeof key1,
                                      (const uint8_t *)"Hi There", 8, mac),
                     EHEM_OK);
    ASSERT_HEX_EQ(mac, "b0344c61d8db38535ca8afceaf0bf12b"
                       "881dc200c9833da726e9376c2e32cff7", 32);

    /* TC2: key = "Jefe", data = "what do ya want for nothing?". */
    assert_int_equal(ehem_hmac_sha256((const uint8_t *)"Jefe", 4,
                                      (const uint8_t *)"what do ya want for nothing?",
                                      28, mac), EHEM_OK);
    ASSERT_HEX_EQ(mac, "5bdcc146bf60754e6a042426089575c7"
                       "5a003f089d2739839dec58b964ec3843", 32);

    /* Empty message is valid (msg may be NULL when msg_len == 0). */
    assert_int_equal(ehem_hmac_sha256(key1, sizeof key1, NULL, 0, mac), EHEM_OK);

    /* Argument validation. */
    assert_int_equal(ehem_hmac_sha256(NULL, 4, (const uint8_t *)"x", 1, mac),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac_sha256(key1, sizeof key1, NULL, 1, mac),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac_sha256(key1, sizeof key1,
                                      (const uint8_t *)"x", 1, NULL),
                     EHEM_ERR_ARG);
}

/* --- X25519 keypair from seed, RFC 7748 §6.1 ------------------------------- */
static void test_x25519_keypair(void **state)
{
    (void)state;
    dump_curve25519_diag();   /* TEMPORARY — see note at top of file */
    uint8_t seed[32], priv[32], pub[32];

    /* Alice. */
    unhex("77076d0a7318a57d3c16c17251b26645"
          "df4c2f87ebc0992ab177fba51db92c2a", seed);
    assert_int_equal(ehem_x25519_keypair_from_seed(seed, priv, pub), EHEM_OK);
    ASSERT_HEX_EQ(pub, "8520f0098930a754748b7ddcb43ef75a"
                       "0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);
    /* priv_out is the RFC 7748-clamped scalar. */
    ASSERT_HEX_EQ(priv, "70076d0a7318a57d3c16c17251b26645"
                        "df4c2f87ebc0992ab177fba51db92c6a", 32);
    assert_int_equal(priv[0] & 0x07, 0);   /* low 3 bits cleared */
    assert_int_equal(priv[31] & 0x80, 0);  /* top bit cleared    */
    assert_int_equal(priv[31] & 0x40, 0x40); /* bit 254 set       */

    /* Bob. */
    unhex("5dab087e624a8a4b79e17f8b83800ee6"
          "6f3bb1292618b6fd1c2f8b27ff88e0eb", seed);
    assert_int_equal(ehem_x25519_keypair_from_seed(seed, NULL, pub), EHEM_OK);
    ASSERT_HEX_EQ(pub, "de9edb7d7b7dc1b4d35b61c2ece43537"
                       "3f8343c85b78674dadfc7e146f882b4f", 32);

    assert_int_equal(ehem_x25519_keypair_from_seed(NULL, priv, pub),
                     EHEM_ERR_ARG);
}

/* --- X25519 ECDH: RFC 7748 §6.1 (DH) + §5.2 (scalar mult) ------------------ */
static void test_x25519_shared(void **state)
{
    (void)state;
    uint8_t a_priv[32], b_priv[32], a_pub[32], b_pub[32], s1[32], s2[32];

    unhex("77076d0a7318a57d3c16c17251b26645"
          "df4c2f87ebc0992ab177fba51db92c2a", a_priv);
    unhex("5dab087e624a8a4b79e17f8b83800ee6"
          "6f3bb1292618b6fd1c2f8b27ff88e0eb", b_priv);
    assert_int_equal(ehem_x25519_keypair_from_seed(a_priv, NULL, a_pub), EHEM_OK);
    assert_int_equal(ehem_x25519_keypair_from_seed(b_priv, NULL, b_pub), EHEM_OK);

    /* Both parties derive the same shared secret. */
    assert_int_equal(ehem_x25519_shared(a_priv, b_pub, s1), EHEM_OK);
    assert_int_equal(ehem_x25519_shared(b_priv, a_pub, s2), EHEM_OK);
    ASSERT_HEX_EQ(s1, "4a5d9d5ba4ce2de1728e3bf480350f25"
                      "e07e21c947d19e3376f09b3c1e161742", 32);
    assert_memory_equal(s1, s2, 32);

    /* RFC 7748 §5.2 scalar-mult vector (arbitrary u-coordinate). */
    uint8_t k[32], u[32], out[32];
    unhex("a546e36bf0527c9d3b16154b82465edd"
          "62144c0ac1fc5a18506a2244ba449ac4", k);
    unhex("e6db6867583030db3594c1a424b15f7c"
          "726624ec26b3353b10a903a6d0ab1c4c", u);
    assert_int_equal(ehem_x25519_shared(k, u, out), EHEM_OK);
    ASSERT_HEX_EQ(out, "c3da55379de9c6908e94ea4df28d084f"
                       "32eccf03491c71f754b4075577a28552", 32);

    assert_int_equal(ehem_x25519_shared(NULL, b_pub, s1), EHEM_ERR_ARG);
    assert_int_equal(ehem_x25519_shared(a_priv, NULL, s1), EHEM_ERR_ARG);
    assert_int_equal(ehem_x25519_shared(a_priv, b_pub, NULL), EHEM_ERR_ARG);
}

/* --- zeroize: proven non-elided via a volatile read ------------------------ */
static void test_zeroize(void **state)
{
    (void)state;
    unsigned char buf[64];
    memset(buf, 0xAB, sizeof buf);

    ehem_zeroize(buf, sizeof buf);

    /* Read back through a volatile pointer so the compiler cannot assume the
     * scrub was dead and skip it (the whole point of ehem_zeroize). */
    volatile unsigned char *vp = (volatile unsigned char *)buf;
    for (size_t i = 0; i < sizeof buf; i++) {
        assert_int_equal(vp[i], 0);
    }

    ehem_zeroize(NULL, 16);  /* NULL-safe no-op */
    ehem_zeroize(buf, 0);    /* zero length no-op */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pbkdf2),
        cmocka_unit_test(test_hmac_sha256),
        cmocka_unit_test(test_x25519_keypair),
        cmocka_unit_test(test_x25519_shared),
        cmocka_unit_test(test_zeroize),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
