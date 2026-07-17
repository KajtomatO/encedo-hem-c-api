/*
 * test_ecdh_live.c — live ECDH: X25519 byte-exact cross-check against the
 * crypto shim, hashed-variant check, NIST ext_kid symmetry, family-mismatch
 * negative, and the REQ-OPS-004 raw-mode truncation probe on P-384.
 *
 * verifies: REQ-OPS-004 (live: X25519 pubkey-mode secret == local
 *           ehem_x25519_shared against the device key's pubkey from
 *           ehem_key_get, byte-exact; SHA2-256 variant == local SHA-256 of
 *           that secret; NIST ext_kid symmetry ecdh(A,B) == ecdh(B,A);
 *           family mismatch → EHEM_ERR_DEVICE 406; PROBE: raw-mode output
 *           length for SECP384R1 — firmware predicts 32 [truncated], doc
 *           claims 48 — recorded in REQ-OPS-004)
 *
 * Links the STATIC lib + internal headers (via ehem_test_support) for the
 * crypto-shim X25519 and wolfCrypt SHA-256 — the same exception
 * test_sign_live takes. EHEMTEST key hygiene per REQ-TEST-003.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/hash.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "crypto_shim.h"      /* internal: local X25519 for the cross-check */
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct ecdh_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} ecdh_state;

static int setup(void **state)
{
    ecdh_state *s = calloc(1, sizeof *s);
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
    ecdh_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

/* Create an EHEMTEST key of `type` (optional mode), tracked for cleanup. */
static void create_key(ecdh_state *s, const char *type, const char *mode,
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

/* -------------------------------------------------------------------------- */
/* X25519: byte-exact against the shim; hashed variant against local SHA-256   */
/* -------------------------------------------------------------------------- */

static void test_x25519_local_crosscheck(void **state)
{
    ecdh_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    static const uint8_t seed[EHEM_X25519_KEYSIZE] = {
        0x8f, 0x1c, 0x02, 0xdd, 0x21, 0x7a, 0x5b, 0x33,
        0x44, 0x95, 0x06, 0xe7, 0xc8, 0x39, 0x0a, 0x1b,
        0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0x82, 0x93,
        0xa4, 0xb5, 0xc6, 0xd7, 0xe8, 0xf9, 0x0a, 0x1b,
    };
    uint8_t local_priv[EHEM_X25519_KEYSIZE];
    uint8_t local_pub[EHEM_X25519_KEYSIZE];
    uint8_t local_secret[EHEM_X25519_KEYSIZE];
    uint8_t local_hash[EHEM_SHA256_SIZE];
    ehem_key_details *d = NULL;
    ehem_ecdh_secret *dev_raw = NULL;
    ehem_ecdh_secret *dev_hashed = NULL;

    create_key(s, "CURVE25519", NULL, kid);

    assert_int_equal(ehem_x25519_keypair_from_seed(seed, local_priv,
                                                   local_pub), EHEM_OK);

    /* Device's public key (raw 32 bytes LE for Curve25519, REQ-KEY-006). */
    assert_int_equal(ehem_key_get(s->ctx, kid, &d), EHEM_OK);
    assert_non_null(d->pubkey);
    assert_int_equal((int)d->pubkey_len, EHEM_X25519_KEYSIZE);

    /* Local side of the exchange. */
    assert_int_equal(ehem_x25519_shared(local_priv, d->pubkey, local_secret),
                     EHEM_OK);

    /* Device side, raw: must match byte-for-byte (X25519 secret is 32 —
     * unaffected by the fw raw-mode 32-byte cap). */
    assert_int_equal(ehem_ecdh(s->ctx, kid, NULL, local_pub,
                               sizeof local_pub, NULL, &dev_raw), EHEM_OK);
    assert_int_equal((int)dev_raw->secret_len, EHEM_X25519_KEYSIZE);
    assert_memory_equal(dev_raw->secret, local_secret, EHEM_X25519_KEYSIZE);

    /* Hashed variant digests the full secret. */
    assert_int_equal(wc_Sha256Hash(local_secret, sizeof local_secret,
                                   local_hash), 0);
    assert_int_equal(ehem_ecdh(s->ctx, kid, NULL, local_pub,
                               sizeof local_pub, EHEM_HASH_ALG_SHA2_256,
                               &dev_hashed), EHEM_OK);
    assert_int_equal((int)dev_hashed->secret_len, EHEM_SHA256_SIZE);
    assert_memory_equal(dev_hashed->secret, local_hash, EHEM_SHA256_SIZE);

    ehem_ecdh_secret_free(dev_raw);
    ehem_ecdh_secret_free(dev_hashed);
    ehem_key_details_free(d);
    ehem_zeroize(local_priv, sizeof local_priv);
    ehem_zeroize(local_secret, sizeof local_secret);
}

/* -------------------------------------------------------------------------- */
/* NIST ext_kid symmetry + family mismatch                                     */
/* -------------------------------------------------------------------------- */

static void test_nist_ext_kid_symmetry_and_mismatch(void **state)
{
    ecdh_state *s = *state;
    char kid_a[EHEM_KID_HEX_SIZE], kid_b[EHEM_KID_HEX_SIZE];
    char kid_x[EHEM_KID_HEX_SIZE];
    ehem_ecdh_secret *ab = NULL;
    ehem_ecdh_secret *ba = NULL;
    ehem_ecdh_secret *bad = NULL;

    create_key(s, "SECP256R1", "ECDH", kid_a);
    create_key(s, "SECP256R1", "ECDH", kid_b);

    assert_int_equal(ehem_ecdh(s->ctx, kid_a, kid_b, NULL, 0, NULL, &ab),
                     EHEM_OK);
    assert_int_equal(ehem_ecdh(s->ctx, kid_b, kid_a, NULL, 0, NULL, &ba),
                     EHEM_OK);
    assert_int_equal((int)ab->secret_len, 32);
    assert_int_equal((int)ab->secret_len, (int)ba->secret_len);
    assert_memory_equal(ab->secret, ba->secret, ab->secret_len);

    /* Family mismatch: P-256 against an X25519 peer → device 406. */
    create_key(s, "CURVE25519", NULL, kid_x);
    assert_int_equal(ehem_ecdh(s->ctx, kid_a, kid_x, NULL, 0, NULL, &bad),
                     EHEM_ERR_DEVICE);
    assert_null(bad);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);

    ehem_ecdh_secret_free(ab);
    ehem_ecdh_secret_free(ba);
}

/* -------------------------------------------------------------------------- */
/* REQ-OPS-004 open criterion: raw-mode length on P-384                        */
/* -------------------------------------------------------------------------- */

static void test_p384_raw_truncation_probe(void **state)
{
    ecdh_state *s = *state;
    char kid_a[EHEM_KID_HEX_SIZE], kid_b[EHEM_KID_HEX_SIZE];
    ehem_ecdh_secret *raw_ab = NULL;
    ehem_ecdh_secret *raw_ba = NULL;
    ehem_ecdh_secret *hashed = NULL;

    create_key(s, "SECP384R1", "ECDH", kid_a);
    create_key(s, "SECP384R1", "ECDH", kid_b);

    assert_int_equal(ehem_ecdh(s->ctx, kid_a, kid_b, NULL, 0, NULL, &raw_ab),
                     EHEM_OK);
    assert_int_equal(ehem_ecdh(s->ctx, kid_b, kid_a, NULL, 0, NULL, &raw_ba),
                     EHEM_OK);

    /* THE PROBE (resolved 2026-07-17): the device returns 32 — raw mode
     * truncates >256-bit secrets exactly as crypto.c:1679 predicts; the
     * doc's "full curve length" is wrong (REQ-OPS-004 records this).
     * Pinned so a firmware fix shows up as a test failure to investigate. */
    printf("REQ-OPS-004 PROBE: P-384 raw ecdh length = %zu "
           "(live-confirmed truncation; doc claimed 48)\n",
           raw_ab->secret_len);
    assert_int_equal((int)raw_ab->secret_len, 32);
    assert_int_equal((int)raw_ab->secret_len, (int)raw_ba->secret_len);
    assert_memory_equal(raw_ab->secret, raw_ba->secret, raw_ab->secret_len);

    /* Hashed variant digests the FULL 48-byte secret: SHA2-384 → 48 bytes. */
    assert_int_equal(ehem_ecdh(s->ctx, kid_a, kid_b, NULL, 0,
                               EHEM_HASH_ALG_SHA2_384, &hashed), EHEM_OK);
    assert_int_equal((int)hashed->secret_len, 48);

    ehem_ecdh_secret_free(raw_ab);
    ehem_ecdh_secret_free(raw_ba);
    ehem_ecdh_secret_free(hashed);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_x25519_local_crosscheck),
        cmocka_unit_test(test_nist_ext_kid_symmetry_and_mismatch),
        cmocka_unit_test(test_p384_raw_truncation_probe),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
