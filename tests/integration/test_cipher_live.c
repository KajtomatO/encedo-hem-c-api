/*
 * test_cipher_live.c — live AES cipher: GCM/CBC/ECB round-trips, tamper and
 * width-quirk negatives, IV freshness, and the REQ-OPS-006 HKDF-info probe
 * that settles the "encedo-aes" vs "encedo" doc/firmware conflict with a
 * locally-known ECDH secret.
 *
 * verifies: REQ-OPS-006 (live: GCM round-trip ±aad on an EHEMTEST AES-256
 *           key with 16-byte iv+tag; flipped tag bit and wrong aad → 406;
 *           CBC round-trips a non-block-aligned msg; ECB round-trips with
 *           NO iv; same msg twice → different iv; AES128-GCM on the AES-256
 *           key succeeds [width truncation] while AES256-GCM on an AES-128
 *           key → 406; PROBE: derived-mode AES key = HKDF-SHA256(secret,
 *           info="encedo-aes"‖ctx) — reproduced locally byte-for-byte)
 *
 * Links the STATIC lib + internal headers (shim X25519) + wolfCrypt
 * (HKDF, AES-GCM) — same exception as test_ecdh_live. EHEMTEST hygiene per
 * REQ-TEST-003.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/kdf.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "crypto_shim.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct cipher_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} cipher_state;

static int setup(void **state)
{
    cipher_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    cipher_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void create_key(cipher_state *s, const char *type, const char *mode,
                       char kid[EHEM_KID_HEX_SIZE])
{
    ehem_key_create_params p;
    char label[32];

    memset(&p, 0, sizeof p);
    ehem_test_label(label, sizeof label);
    p.type = type;
    p.label = label;
    p.mode = mode;
    assert_int_equal(ehem_key_create(s->ctx, &p, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);
}

static const uint8_t PT[] = "ciphered by test_cipher_live on the HEM!";
#define PT_LEN (sizeof PT - 1)   /* 40 bytes — deliberately not a multiple */
static const uint8_t AAD[4] = { 'a', 'a', 'd', '!' };

/* -------------------------------------------------------------------------- */
/* GCM round-trip + tamper negatives + IV freshness (one AES-256 key)          */
/* -------------------------------------------------------------------------- */

static void test_gcm_roundtrip_tamper_iv(void **state)
{
    cipher_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    ehem_ciphertext *c1 = NULL, *c2 = NULL;
    ehem_plaintext *pt = NULL;

    create_key(s, "AES256", NULL, kid);

    /* Round-trip with aad. */
    assert_int_equal(ehem_encrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  PT, PT_LEN, AAD, sizeof AAD,
                                  NULL, NULL, 0, NULL, 0, &c1), EHEM_OK);
    assert_true(c1->has_iv);
    assert_true(c1->has_tag);
    assert_int_equal((int)c1->ciphertext_len, (int)PT_LEN);  /* GCM: no pad */

    assert_int_equal(ehem_decrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  c1->ciphertext, c1->ciphertext_len,
                                  c1->iv, 16, c1->tag, 16, AAD, sizeof AAD,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_OK);
    assert_int_equal((int)pt->plaintext_len, (int)PT_LEN);
    assert_memory_equal(pt->plaintext, PT, PT_LEN);
    ehem_plaintext_free(pt);
    pt = NULL;

    /* Flipped tag bit → 406. */
    c1->tag[3] ^= 0x01;
    assert_int_equal(ehem_decrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  c1->ciphertext, c1->ciphertext_len,
                                  c1->iv, 16, c1->tag, 16, AAD, sizeof AAD,
                                  NULL, NULL, 0, NULL, 0, &pt),
                     EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);
    c1->tag[3] ^= 0x01;

    /* Wrong aad → 406. */
    static const uint8_t wrong_aad[4] = { 'n', 'o', 'p', 'e' };
    assert_int_equal(ehem_decrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  c1->ciphertext, c1->ciphertext_len,
                                  c1->iv, 16, c1->tag, 16,
                                  wrong_aad, sizeof wrong_aad,
                                  NULL, NULL, 0, NULL, 0, &pt),
                     EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);

    /* Round-trip without aad + IV freshness across two encrypts. */
    assert_int_equal(ehem_encrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &c2), EHEM_OK);
    assert_memory_not_equal(c1->iv, c2->iv, 16);   /* fresh IV every call */
    assert_int_equal(ehem_decrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  c2->ciphertext, c2->ciphertext_len,
                                  c2->iv, 16, c2->tag, 16, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_OK);
    assert_memory_equal(pt->plaintext, PT, PT_LEN);

    ehem_plaintext_free(pt);
    ehem_ciphertext_free(c1);
    ehem_ciphertext_free(c2);
}

/* -------------------------------------------------------------------------- */
/* CBC (non-aligned msg) + ECB (no iv) + width quirk                           */
/* -------------------------------------------------------------------------- */

static void test_cbc_ecb_and_width_quirk(void **state)
{
    cipher_state *s = *state;
    char kid256[EHEM_KID_HEX_SIZE], kid128[EHEM_KID_HEX_SIZE];
    ehem_ciphertext *ct = NULL;
    ehem_plaintext *pt = NULL;

    create_key(s, "AES256", NULL, kid256);

    /* CBC: 40-byte msg → padded to 48 by the device; strips on decrypt. */
    assert_int_equal(ehem_encrypt(s->ctx, kid256, EHEM_CIPHER_ALG_AES256_CBC,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    assert_true(ct->has_iv);
    assert_false(ct->has_tag);
    assert_int_equal((int)ct->ciphertext_len, 48);
    assert_int_equal(ehem_decrypt(s->ctx, kid256, EHEM_CIPHER_ALG_AES256_CBC,
                                  ct->ciphertext, ct->ciphertext_len,
                                  ct->iv, 16, NULL, 0, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_OK);
    assert_int_equal((int)pt->plaintext_len, (int)PT_LEN);
    assert_memory_equal(pt->plaintext, PT, PT_LEN);
    ehem_plaintext_free(pt);
    pt = NULL;
    ehem_ciphertext_free(ct);
    ct = NULL;

    /* ECB: block-aligned input, NO iv in the response. */
    assert_int_equal(ehem_encrypt(s->ctx, kid256, EHEM_CIPHER_ALG_AES256_ECB,
                                  PT, 32, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    assert_false(ct->has_iv);
    assert_false(ct->has_tag);
    assert_int_equal((int)ct->ciphertext_len, 32);
    assert_int_equal(ehem_decrypt(s->ctx, kid256, EHEM_CIPHER_ALG_AES256_ECB,
                                  ct->ciphertext, ct->ciphertext_len,
                                  NULL, 0, NULL, 0, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_OK);
    assert_memory_equal(pt->plaintext, PT, 32);
    ehem_plaintext_free(pt);
    pt = NULL;
    ehem_ciphertext_free(ct);
    ct = NULL;

    /* Width quirk (REQ-OPS-006): requested width ≤ stored width. */
    assert_int_equal(ehem_encrypt(s->ctx, kid256, EHEM_CIPHER_ALG_AES128_GCM,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    ehem_ciphertext_free(ct);
    ct = NULL;

    create_key(s, "AES128", NULL, kid128);
    assert_int_equal(ehem_encrypt(s->ctx, kid128, EHEM_CIPHER_ALG_AES256_GCM,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_DEVICE);
    assert_null(ct);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);
}

/* -------------------------------------------------------------------------- */
/* REQ-OPS-006 open criterion: HKDF info "encedo-aes" vs doc "encedo"          */
/* -------------------------------------------------------------------------- */

/* Locally reproduce the derived-mode AES-256-GCM encryption for a candidate
 * HKDF info string; returns true when ciphertext+tag match the device's. */
static bool candidate_matches(const uint8_t secret[EHEM_X25519_KEYSIZE],
                              const char *info_prefix, const uint8_t *ctx_sfx,
                              size_t ctx_sfx_len, const ehem_ciphertext *dev)
{
    uint8_t info[80];
    size_t info_len = strlen(info_prefix);
    uint8_t key[32];
    uint8_t ct[PT_LEN];
    uint8_t tag[16];
    Aes aes;
    int ret;

    memcpy(info, info_prefix, info_len);
    if (ctx_sfx != NULL) {
        memcpy(info + info_len, ctx_sfx, ctx_sfx_len);
        info_len += ctx_sfx_len;
    }
    if (wc_HKDF(WC_SHA256, secret, EHEM_X25519_KEYSIZE, NULL, 0,
                info, (word32)info_len, key, sizeof key) != 0) {
        return false;
    }
    if (wc_AesInit(&aes, NULL, INVALID_DEVID) != 0) {
        return false;
    }
    ret = wc_AesGcmSetKey(&aes, key, sizeof key);
    if (ret == 0) {
        ret = wc_AesGcmEncrypt(&aes, ct, PT, PT_LEN, dev->iv, 16,
                               tag, 16, NULL, 0);
    }
    wc_AesFree(&aes);
    if (ret != 0) {
        return false;
    }
    return dev->ciphertext_len == PT_LEN &&
           memcmp(ct, dev->ciphertext, PT_LEN) == 0 &&
           memcmp(tag, dev->tag, 16) == 0;
}

static void test_derived_hkdf_info_probe(void **state)
{
    cipher_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    static const uint8_t seed[EHEM_X25519_KEYSIZE] = {
        0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22,
        0x11, 0x00, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa,
        0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22,
        0x11, 0x00, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa,
    };
    static const uint8_t ctx_sfx[4] = { 'c', 't', 'x', '1' };
    uint8_t local_priv[EHEM_X25519_KEYSIZE];
    uint8_t local_pub[EHEM_X25519_KEYSIZE];
    uint8_t secret[EHEM_X25519_KEYSIZE];
    ehem_key_details *d = NULL;
    ehem_ciphertext *dev = NULL;

    create_key(s, "CURVE25519", NULL, kid);
    assert_int_equal(ehem_x25519_keypair_from_seed(seed, local_priv,
                                                   local_pub), EHEM_OK);
    assert_int_equal(ehem_key_get(s->ctx, kid, &d), EHEM_OK);
    assert_int_equal((int)d->pubkey_len, EHEM_X25519_KEYSIZE);
    assert_int_equal(ehem_x25519_shared(local_priv, d->pubkey, secret),
                     EHEM_OK);

    /* No ctx suffix: info should be exactly the firmware prefix. */
    assert_int_equal(ehem_encrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, local_pub, sizeof local_pub,
                                  NULL, 0, &dev), EHEM_OK);
    assert_true(dev->has_iv && dev->has_tag);

    bool fw_info = candidate_matches(secret, "encedo-aes", NULL, 0, dev);
    bool doc_info = candidate_matches(secret, "encedo", NULL, 0, dev);
    printf("REQ-OPS-006 PROBE: HKDF info \"encedo-aes\" %s, \"encedo\" %s\n",
           fw_info ? "MATCHES" : "no", doc_info ? "MATCHES" : "no");
    assert_true(fw_info);
    assert_false(doc_info);
    ehem_ciphertext_free(dev);
    dev = NULL;

    /* With a ctx suffix: info = "encedo-aes" ‖ ctx bytes. */
    assert_int_equal(ehem_encrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  PT, PT_LEN, NULL, 0,
                                  NULL, local_pub, sizeof local_pub,
                                  ctx_sfx, sizeof ctx_sfx, &dev), EHEM_OK);
    assert_true(candidate_matches(secret, "encedo-aes", ctx_sfx,
                                  sizeof ctx_sfx, dev));

    /* And the device round-trips its own derived ciphertext. */
    ehem_plaintext *pt = NULL;
    assert_int_equal(ehem_decrypt(s->ctx, kid, EHEM_CIPHER_ALG_AES256_GCM,
                                  dev->ciphertext, dev->ciphertext_len,
                                  dev->iv, 16, dev->tag, 16, NULL, 0,
                                  NULL, local_pub, sizeof local_pub,
                                  ctx_sfx, sizeof ctx_sfx, &pt), EHEM_OK);
    assert_memory_equal(pt->plaintext, PT, PT_LEN);

    ehem_plaintext_free(pt);
    ehem_ciphertext_free(dev);
    ehem_key_details_free(d);
    ehem_zeroize(local_priv, sizeof local_priv);
    ehem_zeroize(secret, sizeof secret);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_gcm_roundtrip_tamper_iv),
        cmocka_unit_test(test_cbc_ecb_and_width_quirk),
        cmocka_unit_test(test_derived_hkdf_info_probe),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
