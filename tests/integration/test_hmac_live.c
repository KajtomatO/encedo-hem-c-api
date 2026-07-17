/*
 * test_hmac_live.c — live HMAC: direct-mode round-trips on HMAC keys, the
 * tamper negative, and the REQ-OPS-005 derived-key probe that settles the
 * doc/firmware HKDF conflict with a locally-known ECDH secret.
 *
 * verifies: REQ-OPS-005 (live: SHA2-256 HMAC key hash→verify EHEM_OK, one
 *           flipped mac bit → 406; SHA3-384 key round-trips with a 48-byte
 *           MAC; PROBE: derived mode via pubkey with a local shim X25519
 *           keypair — the device MAC is compared against local HMAC-SHA256
 *           keyed with the RAW ECDH secret [firmware prediction: no HKDF])
 *
 * Links the STATIC lib + internal headers (via ehem_test_support) for the
 * crypto-shim X25519/HMAC — same exception as test_ecdh_live. EHEMTEST key
 * hygiene per REQ-TEST-003.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "crypto_shim.h"      /* internal: local X25519 + HMAC-SHA256 */
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct hmac_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} hmac_state;

static int setup(void **state)
{
    hmac_state *s = calloc(1, sizeof *s);
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
    hmac_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void create_key(hmac_state *s, const char *type, const char *mode,
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

static const uint8_t MSG[] = "mac'd by test_hmac_live via the Encedo HEM";
#define MSG_LEN (sizeof MSG - 1)

/* -------------------------------------------------------------------------- */
/* direct mode: hash → verify round-trips + tamper                             */
/* -------------------------------------------------------------------------- */

static void test_direct_sha2_roundtrip_and_tamper(void **state)
{
    hmac_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    ehem_mac *m = NULL;

    create_key(s, "SHA2-256", NULL, kid);

    assert_int_equal(ehem_hmac(s->ctx, kid, NULL, MSG, MSG_LEN,
                               NULL, NULL, 0, &m), EHEM_OK);
    assert_non_null(m);
    assert_int_equal((int)m->mac_len, 32);

    assert_int_equal(ehem_hmac_verify(s->ctx, kid, NULL, MSG, MSG_LEN,
                                      m->mac, m->mac_len, NULL, NULL, 0),
                     EHEM_OK);

    m->mac[7] ^= 0x01;
    assert_int_equal(ehem_hmac_verify(s->ctx, kid, NULL, MSG, MSG_LEN,
                                      m->mac, m->mac_len, NULL, NULL, 0),
                     EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(s->ctx)->http_status, 406);

    ehem_mac_free(m);
}

static void test_direct_sha3_family_length(void **state)
{
    hmac_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    ehem_mac *m = NULL;

    create_key(s, "SHA3-384", NULL, kid);

    assert_int_equal(ehem_hmac(s->ctx, kid, NULL, MSG, MSG_LEN,
                               NULL, NULL, 0, &m), EHEM_OK);
    assert_int_equal((int)m->mac_len, 48);   /* the key's family decides */
    assert_int_equal(ehem_hmac_verify(s->ctx, kid, NULL, MSG, MSG_LEN,
                                      m->mac, m->mac_len, NULL, NULL, 0),
                     EHEM_OK);

    ehem_mac_free(m);
}

/* -------------------------------------------------------------------------- */
/* REQ-OPS-005 open criterion: derived-mode key = raw ECDH secret vs HKDF      */
/* -------------------------------------------------------------------------- */

static void test_derived_hkdf_conflict_probe(void **state)
{
    hmac_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE];
    static const uint8_t seed[EHEM_X25519_KEYSIZE] = {
        0x51, 0x62, 0x73, 0x84, 0x95, 0xa6, 0xb7, 0xc8,
        0xd9, 0xea, 0xfb, 0x0c, 0x1d, 0x2e, 0x3f, 0x40,
        0x51, 0x62, 0x73, 0x84, 0x95, 0xa6, 0xb7, 0xc8,
        0xd9, 0xea, 0xfb, 0x0c, 0x1d, 0x2e, 0x3f, 0x40,
    };
    uint8_t local_priv[EHEM_X25519_KEYSIZE];
    uint8_t local_pub[EHEM_X25519_KEYSIZE];
    uint8_t secret[EHEM_X25519_KEYSIZE];
    uint8_t local_mac[EHEM_SHA256_SIZE];
    ehem_key_details *d = NULL;
    ehem_mac *dev = NULL;

    create_key(s, "CURVE25519", NULL, kid);

    assert_int_equal(ehem_x25519_keypair_from_seed(seed, local_priv,
                                                   local_pub), EHEM_OK);
    assert_int_equal(ehem_key_get(s->ctx, kid, &d), EHEM_OK);
    assert_non_null(d->pubkey);
    assert_int_equal((int)d->pubkey_len, EHEM_X25519_KEYSIZE);
    assert_int_equal(ehem_x25519_shared(local_priv, d->pubkey, secret),
                     EHEM_OK);

    /* Device MAC in the derived flow (alg required). */
    assert_int_equal(ehem_hmac(s->ctx, kid, EHEM_HASH_ALG_SHA2_256,
                               MSG, MSG_LEN, NULL, local_pub,
                               sizeof local_pub, &dev), EHEM_OK);
    assert_int_equal((int)dev->mac_len, EHEM_SHA256_SIZE);

    /* Firmware prediction (crypto.c:541-559): the HMAC key is the RAW ECDH
     * secret — no HKDF, contradicting the doc's "ECDH + HKDF". */
    assert_int_equal(ehem_hmac_sha256(secret, sizeof secret, MSG, MSG_LEN,
                                      local_mac), EHEM_OK);
    if (memcmp(dev->mac, local_mac, EHEM_SHA256_SIZE) == 0) {
        printf("REQ-OPS-005 PROBE: derived-mode HMAC key = RAW ECDH secret "
               "(no HKDF) — firmware confirmed, doc's HKDF claim wrong\n");
    } else {
        printf("REQ-OPS-005 PROBE: device MAC does NOT match the raw-secret "
               "candidate — investigate (HKDF variant?)\n");
    }
    assert_memory_equal(dev->mac, local_mac, EHEM_SHA256_SIZE);

    /* And the device agrees with its own derived verify. */
    assert_int_equal(ehem_hmac_verify(s->ctx, kid, EHEM_HASH_ALG_SHA2_256,
                                      MSG, MSG_LEN, dev->mac, dev->mac_len,
                                      NULL, local_pub, sizeof local_pub),
                     EHEM_OK);

    ehem_mac_free(dev);
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
        cmocka_unit_test(test_direct_sha2_roundtrip_and_tamper),
        cmocka_unit_test(test_direct_sha3_family_length),
        cmocka_unit_test(test_derived_hkdf_conflict_probe),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
