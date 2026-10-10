/*
 * test_bip39.c — Manager-compatible master-secret derivation (REQ-AUTH-012).
 *
 * verifies: REQ-AUTH-012 (three 24-word mnemonics derive the exact 32 bytes
 *           Encedo Manager's OWN code produces — fixtures computed by running
 *           assets/sjcl-bip39_v1.js + assets/jsbip39_v1.js +
 *           assets/wordlist_english_v1.js of encedo-manager b33c236 under
 *           node 24: `new Mnemonic("english").toSeed(words)` and its
 *           `.substr(1, 64)` slice (the Manager's build.js:697 / :6829);
 *           the nibble shift is proven against the un-shifted seed;
 *           checksum / word-count / unknown-word / upper-case rejections
 *           match jsbip39's check(); whitespace canonicalisation matches
 *           toSeed(); generation yields 24 valid words that round-trip)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "bip39.h"            /* internal: the entropy → words encoder */
#include "crypto_shim.h"      /* internal: PBKDF2-SHA512 to prove the shift */

/* Fixture command (scratchpad bip39_fixture.js, 2026-10-08):
 *   node: vm-load sjcl-bip39_v1.js, wordlist_english_v1.js, jsbip39_v1.js;
 *   m = new Mnemonic("english"); seed = m.toSeed(words); secret = seed.substr(1, 64)
 */
static const struct {
    const char *words;
    const char *seed_hex;     /* 128 hex chars — jsbip39 toSeed() */
    const char *secret_hex;   /* 64 hex chars — seed.substr(1, 64) */
} VECTORS[] = {
    { "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
      "abandon abandon abandon abandon abandon art",
      "408b285c123836004f4b8842c89324c1f01382450c0d439af345ba7fc49acf70"
      "5489c6fc77dbd4e3dc1dd8cc6bc9f043db8ada1e243c4a0eafb290d399480840",
      "08b285c123836004f4b8842c89324c1f01382450c0d439af345ba7fc49acf705" },
    { "letter advice cage absurd amount doctor acoustic avoid letter advice "
      "cage absurd amount doctor acoustic avoid letter advice cage absurd "
      "amount doctor acoustic bless",
      "848bbe19cad445e46f35fd3d1a89463583ac2b60b5eb4cfcf955731775a5d9e1"
      "7a81a71613fed83f1ae27b408478fdec2bbc75b5161d1937aa7cdf4ad686ef5f",
      "48bbe19cad445e46f35fd3d1a89463583ac2b60b5eb4cfcf955731775a5d9e17" },
    { "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo "
      "zoo zoo zoo zoo zoo vote",
      "e28a37058c7f5112ec9e16a3437cf363a2572d70b6ceb3b6965447623d620f14"
      "d06bb321a26b33ec15fcd84a3b5ddfd5520e230c924c87aaa0d559749e044fef",
      "28a37058c7f5112ec9e16a3437cf363a2572d70b6ceb3b6965447623d620f14d" },
};

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

/* Every vector: the public helper matches the Manager byte for byte, the
 * standard seed matches too, and the secret is the nibble-SHIFTED seed (not its
 * first 32 bytes) — the Manager's substr(1, 64), reproduced on purpose. */
static void test_vectors_match_manager(void **state)
{
    (void)state;
    for (size_t v = 0; v < sizeof VECTORS / sizeof VECTORS[0]; v++) {
        uint8_t expect_secret[32], expect_seed[64], secret[32], seed[64], shifted[32];
        assert_int_equal(unhex(VECTORS[v].secret_hex, expect_secret), 32);
        assert_int_equal(unhex(VECTORS[v].seed_hex, expect_seed), 64);

        assert_int_equal(ehem_master_secret_from_mnemonic(NULL, VECTORS[v].words, secret),
                         EHEM_OK);
        assert_memory_equal(secret, expect_secret, 32);

        /* The standard BIP39 seed (PBKDF2-HMAC-SHA512, "mnemonic", 2048). */
        assert_int_equal(ehem_kdf_pbkdf2_sha512((const uint8_t *)VECTORS[v].words,
                                                strlen(VECTORS[v].words),
                                                (const uint8_t *)"mnemonic", 8,
                                                2048, seed, sizeof seed), EHEM_OK);
        assert_memory_equal(seed, expect_seed, 64);

        /* secret == seed << 4 bits; and NOT seed[0..32) (the un-shifted slice). */
        for (int i = 0; i < 32; i++) {
            shifted[i] = (uint8_t)((seed[i] << 4) | (seed[i + 1] >> 4));
        }
        assert_memory_equal(secret, shifted, 32);
        assert_memory_not_equal(secret, seed, 32);
    }
}

/* jsbip39's toSeed() re-joins the words with single spaces, so surrounding and
 * repeated whitespace (spaces, tabs, newlines) derive the same secret. */
static void test_whitespace_canonical(void **state)
{
    (void)state;
    uint8_t a[32], b[32];
    const char *messy =
        "  abandon\tabandon   abandon abandon abandon abandon abandon abandon abandon\n"
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon    art \n";
    assert_int_equal(ehem_master_secret_from_mnemonic(NULL, VECTORS[0].words, a), EHEM_OK);
    assert_int_equal(ehem_master_secret_from_mnemonic(NULL, messy, b), EHEM_OK);
    assert_memory_equal(a, b, 32);
}

/* The rejections jsbip39's check() gives (all false there): a wrong last word
 * (checksum), 23 words, an unknown word, an upper-case word. With a context the
 * reason lands in last-error; the output buffer is never written. */
static void test_rejections(void **state)
{
    (void)state;
    uint8_t secret[32];
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);

    const char *bad_checksum =
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon";
    memset(secret, 0xAA, sizeof secret);
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, bad_checksum, secret), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "checksum"));
    for (int i = 0; i < 32; i++) {
        assert_int_equal(secret[i], 0xAA);        /* untouched */
    }

    const char *twenty_three =
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon";
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, twenty_three, secret), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "23 words"));

    const char *unknown =
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon encedo";
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, unknown, secret), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "word 24"));
    assert_non_null(strstr(ehem_last_error(ctx)->message, "encedo"));

    const char *upper =
        "Abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon art";
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, upper, secret), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "word 1"));

    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, "", secret), EHEM_ERR_ARG);
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, NULL, secret), EHEM_ERR_ARG);
    assert_int_equal(ehem_master_secret_from_mnemonic(ctx, VECTORS[0].words, NULL), EHEM_ERR_ARG);

    ehem_ctx_destroy(ctx);
}

/* Known entropy → the BIP39 reference mnemonics (zero entropy → 23×abandon +
 * art; 0x80… → "letter advice…bless"; 0xff… → 23×zoo + vote), so the encoder
 * — not only the decoder — is pinned to the standard. */
static void test_from_entropy(void **state)
{
    (void)state;
    uint8_t entropy[32];
    char words[EHEM_BIP39_MNEMONIC_CAP];

    memset(entropy, 0x00, sizeof entropy);
    assert_int_equal(ehem_bip39_from_entropy(entropy, words, sizeof words), EHEM_OK);
    assert_string_equal(words, VECTORS[0].words);

    memset(entropy, 0x80, sizeof entropy);
    assert_int_equal(ehem_bip39_from_entropy(entropy, words, sizeof words), EHEM_OK);
    assert_string_equal(words, VECTORS[1].words);

    memset(entropy, 0xff, sizeof entropy);
    assert_int_equal(ehem_bip39_from_entropy(entropy, words, sizeof words), EHEM_OK);
    assert_string_equal(words, VECTORS[2].words);

    assert_int_equal(ehem_bip39_from_entropy(entropy, words, 10), EHEM_ERR_ARG);
}

/* Generation: 24 words, valid checksum (the derivation accepts them), two
 * calls differ, the public free scrubs. */
static void test_generate_roundtrip(void **state)
{
    (void)state;
    char *w1 = NULL, *w2 = NULL;
    uint8_t s1[32], s2[32];
    int spaces = 0;

    assert_int_equal(ehem_mnemonic_generate(NULL, &w1), EHEM_OK);
    assert_non_null(w1);
    for (const char *p = w1; *p; p++) {
        spaces += (*p == ' ');
        assert_true((*p >= 'a' && *p <= 'z') || *p == ' ');
    }
    assert_int_equal(spaces, 23);
    assert_int_equal(ehem_master_secret_from_mnemonic(NULL, w1, s1), EHEM_OK);

    assert_int_equal(ehem_mnemonic_generate(NULL, &w2), EHEM_OK);
    assert_string_not_equal(w1, w2);
    assert_int_equal(ehem_master_secret_from_mnemonic(NULL, w2, s2), EHEM_OK);
    assert_memory_not_equal(s1, s2, 32);

    ehem_mnemonic_free(w1);
    ehem_mnemonic_free(w2);
    ehem_mnemonic_free(NULL);
    assert_int_equal(ehem_mnemonic_generate(NULL, NULL), EHEM_ERR_ARG);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vectors_match_manager),
        cmocka_unit_test(test_whitespace_canonical),
        cmocka_unit_test(test_rejections),
        cmocka_unit_test(test_from_entropy),
        cmocka_unit_test(test_generate_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
