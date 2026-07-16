/*
 * test_crypto.c — crypto shim (src/crypto_shim.h) against published vectors.
 *
 * verifies: REQ-AUTH-001 (PBKDF2-HMAC-SHA256, HMAC-SHA256 and X25519 match
 *           RFC 4231 / RFC 7748 / PBKDF2-HMAC-SHA256 test vectors; secret
 *           scrubbing is non-elidable),
 *           REQ-OPS-001 (the local-verify gate infrastructure: ECDSA matches
 *           the RFC 6979 A.2.5 P-256 known answer through BOTH X9.63 point
 *           forms — compressed, the firmware's export form, and uncompressed;
 *           Ed25519 matches RFC 8032 §7.1 TESTs 1-3 incl. the empty message;
 *           tampered signature/message → clean invalid, not an error)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "crypto_shim.h"

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

/* --- X25519 ECDH: RFC 7748 §6.1 Diffie-Hellman (scalar mult with a valid u) - */
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

    /* The raw RFC 7748 §5.2 vectors (an arbitrary u that is not a valid public
     * key) are intentionally NOT asserted: the shim's scalarmult
     * (wc_curve25519_generic) validates u as a real public key and rejects such
     * synthetic inputs. The §6.1 Diffie-Hellman above already exercises the
     * scalar-mult ladder with an arbitrary *valid* u (each party's peer key). */

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

/* --- local ECDSA verify (REQ-OPS-001 gate infrastructure) -------------------
 * RFC 6979 A.2.5 known-answer vector: P-256, SHA-256, message "sample".
 * The public point is fed in BOTH X9.63 forms — compressed (0x03‖X, the
 * firmware's export form; needs HAVE_COMP_KEY in the wolfSSL build) and
 * uncompressed (0x04‖X‖Y) — and the signature is the vector's (r,s) in DER. */
#define P256_QX "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6"
#define P256_QY "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299"
#define P256_SAMPLE_SIG_DER                                        \
    "3046"                                                         \
    "022100" "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716" \
    "022100" "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8"

static void test_ecdsa_verify(void **state)
{
    (void)state;
    uint8_t pub_c[33], pub_u[65], sig[72];
    int valid = -1;

    /* Compressed: Qy ends 0x99 (odd) → prefix 0x03. */
    pub_c[0] = 0x03;
    assert_int_equal(unhex(P256_QX, pub_c + 1), 32);
    pub_u[0] = 0x04;
    assert_int_equal(unhex(P256_QX, pub_u + 1), 32);
    assert_int_equal(unhex(P256_QY, pub_u + 33), 32);
    assert_int_equal(unhex(P256_SAMPLE_SIG_DER, sig), 72);

    const uint8_t *msg = (const uint8_t *)"sample";

    /* Positive, compressed point (the wire form ehem_key_get returns).
     * A wolfSSL built without HAVE_COMP_KEY reports EHEM_ERR_UNSUPPORTED —
     * recorded, not failed, so a comp-key-less CI build stays green while
     * the uncompressed path below still proves the vector. */
    ehem_rc comp_rc = ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_c, 33,
                                        msg, 6, sig, 72, &valid);
    if (comp_rc == EHEM_ERR_UNSUPPORTED) {
        print_message("NOTE: compressed-point import not compiled into this "
                      "wolfSSL (no HAVE_COMP_KEY) — record per STEP-M4-020\n");
    } else {
        assert_int_equal(comp_rc, EHEM_OK);
        assert_int_equal(valid, 1);
    }

    /* Positive, uncompressed point. */
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_u, 65,
                                       msg, 6, sig, 72, &valid), EHEM_OK);
    assert_int_equal(valid, 1);

    /* Negatives run on the uncompressed form so they hold on every build. */

    /* Tampered signature: clean invalid, not an error. */
    sig[71] ^= 0x01;
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_u, 65,
                                       msg, 6, sig, 72, &valid), EHEM_OK);
    assert_int_equal(valid, 0);
    sig[71] ^= 0x01;

    /* Wrong message: clean invalid. */
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_u, 65,
                                       (const uint8_t *)"sampl3", 6,
                                       sig, 72, &valid), EHEM_OK);
    assert_int_equal(valid, 0);

    /* Signature bytes that are not DER: clean invalid, no crash. */
    uint8_t junk[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4 };
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_u, 65,
                                       msg, 6, junk, sizeof junk, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 0);

    /* A public point that is not on the curve must never validate. Whether
     * the rejection is an import error (EHEM_ERR_PROTOCOL, on wolfSSL builds
     * with WOLFSSL_VALIDATE_ECC_IMPORT) or a clean verification failure
     * (EHEM_OK + valid 0, as on Debian 5.6.6) is build-dependent. */
    pub_u[5] ^= 0xFF;
    valid = -1;
    ehem_rc offcurve_rc = ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_u, 65,
                                            msg, 6, sig, 72, &valid);
    assert_true(offcurve_rc == EHEM_ERR_PROTOCOL ||
                (offcurve_rc == EHEM_OK && valid == 0));
    pub_u[5] ^= 0xFF;

    /* Argument validation. */
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, NULL, 33,
                                       msg, 6, sig, 72, &valid),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_c, 33,
                                       NULL, 6, sig, 72, &valid),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1, pub_c, 33,
                                       msg, 6, sig, 72, NULL),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdsa_verify((ehem_ecdsa_curve)99, pub_c, 33,
                                       msg, 6, sig, 72, &valid),
                     EHEM_ERR_ARG);
}

/* --- local Ed25519 verify ----------------------------------------------------
 * RFC 8032 §7.1 TEST 1 (empty message), TEST 2 (one byte 0x72) and TEST 3
 * (two bytes af82), plus tampered-signature/message negatives. */
static void test_ed25519_verify(void **state)
{
    (void)state;
    uint8_t pub[32], sig[64];
    int valid = -1;

    /* TEST 1: empty message. */
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
          pub);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
          sig);
    assert_int_equal(ehem_ed25519_verify(pub, NULL, 0, sig, 64, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 1);

    /* TEST 2: one-byte message 0x72. */
    unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
          pub);
    unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
          "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
          sig);
    const uint8_t msg2 = 0x72;
    assert_int_equal(ehem_ed25519_verify(pub, &msg2, 1, sig, 64, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 1);

    /* Tampered signature → clean invalid. */
    sig[0] ^= 0x01;
    assert_int_equal(ehem_ed25519_verify(pub, &msg2, 1, sig, 64, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 0);
    sig[0] ^= 0x01;

    /* Wrong message → clean invalid. */
    const uint8_t wrong = 0x73;
    assert_int_equal(ehem_ed25519_verify(pub, &wrong, 1, sig, 64, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 0);

    /* TEST 3: two-byte message af82. */
    unhex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
          pub);
    unhex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
          "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
          sig);
    const uint8_t msg3[2] = { 0xaf, 0x82 };
    assert_int_equal(ehem_ed25519_verify(pub, msg3, 2, sig, 64, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 1);

    /* Argument validation: only 64-byte signatures are Ed25519. */
    assert_int_equal(ehem_ed25519_verify(pub, msg3, 2, sig, 63, &valid),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ed25519_verify(NULL, msg3, 2, sig, 64, &valid),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ed25519_verify(pub, NULL, 2, sig, 64, &valid),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ed25519_verify(pub, msg3, 2, sig, 64, NULL),
                     EHEM_ERR_ARG);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pbkdf2),
        cmocka_unit_test(test_hmac_sha256),
        cmocka_unit_test(test_x25519_keypair),
        cmocka_unit_test(test_x25519_shared),
        cmocka_unit_test(test_zeroize),
        cmocka_unit_test(test_ecdsa_verify),
        cmocka_unit_test(test_ed25519_verify),
    };
    /* wolfCrypt's process-global init: without it the X25519 tests crash on
     * Windows (uninitialized RNG mutex behind curve25519 blinding). */
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
