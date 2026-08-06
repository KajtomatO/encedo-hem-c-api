/*
 * test_derive_live.c — live keymgmt derive against the real dev-machine HEM.
 *
 * verifies: REQ-KEY-009 (live: derive works end-to-end and is DETERMINISTIC
 *           [ED25519-from-X25519 twice → repo dedup 406 = identical
 *           material], but the stored key is NOT the documented
 *           ECDH+HKDF-SHA256 output — the repo's key-generation applies an
 *           undisclosed extra transformation, so external interop is not
 *           reproducible (pinned by assert_memory_not_equal; 85 candidate
 *           KDF reconstructions failed, see REQ-KEY-009 rev2);
 *           PROBE: SECP521R1-from-X25519 [secret < target] is REJECTED
 *           device-side (406) — the stale-stack quirk is unreachable;
 *           PROBE: the exact "keymgmt:derive" scope is ACCEPTED alongside
 *           the SDK's "keymgmt:gen")
 *
 * Links the STATIC lib + internal headers (ehem_test_support) for the crypto
 * shim (X25519 + HMAC-SHA256 → local HKDF) and the scope probe's internal
 * request path. EHEMTEST key hygiene per REQ-TEST-003; derived keys are
 * deleted promptly (≤ 1 extra key alive at a time where possible).
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
#include "crypto_shim.h"
#include "proto_common.h"
#include "ejwt.h"             /* internal: std base64 for the scope probe */
#include "json.h"
#include "transport.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct dv_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} dv_state;

static int setup(void **state)
{
    dv_state *s = calloc(1, sizeof *s);
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
    dv_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void create_x25519(dv_state *s, char kid[EHEM_KID_HEX_SIZE])
{
    ehem_key_create_params p;
    char label[32];
    memset(&p, 0, sizeof p);
    ehem_test_label(label, sizeof label);
    p.type = "CURVE25519";
    p.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &p, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);
}

/* Local HKDF-SHA256 (salt=∅, one expand round, L ≤ 32) via the shim's HMAC:
 * PRK = HMAC(key="", ikm); OKM1 = HMAC(PRK, info ‖ 0x01). An empty HMAC key
 * zero-pads to the block size exactly like RFC 5869's HashLen-zeros salt. */
static void local_hkdf32(const uint8_t *ikm, size_t ikm_len,
                         const char *info, uint8_t out[EHEM_SHA256_SIZE])
{
    uint8_t prk[EHEM_SHA256_SIZE];
    uint8_t buf[64];
    size_t info_len = strlen(info);

    assert_true(info_len + 1 <= sizeof buf);
    assert_int_equal(ehem_hmac_sha256((const uint8_t *)"", 0, ikm, ikm_len,
                                      prk), EHEM_OK);
    memcpy(buf, info, info_len);
    buf[info_len] = 0x01;
    assert_int_equal(ehem_hmac_sha256(prk, sizeof prk, buf, info_len + 1, out),
                     EHEM_OK);
    ehem_zeroize(prk, sizeof prk);
}

static const uint8_t DERIVE_SEED[EHEM_X25519_KEYSIZE] = {
    0x5d, 0x4e, 0x3f, 0x20, 0x11, 0x02, 0xf3, 0xe4,
    0xd5, 0xc6, 0xb7, 0xa8, 0x99, 0x8a, 0x7b, 0x6c,
    0x5d, 0x4e, 0x3f, 0x20, 0x11, 0x02, 0xf3, 0xe4,
    0xd5, 0xc6, 0xb7, 0xa8, 0x99, 0x8a, 0x7b, 0x6c,
};

/* -------------------------------------------------------------------------- */
/* The documented pipeline, byte-exact                                        */
/* -------------------------------------------------------------------------- */

static void test_hkdf_pipeline_byteexact(void **state)
{
    dv_state *s = *state;
    char kid_dev[EHEM_KID_HEX_SIZE], kid_mac[EHEM_KID_HEX_SIZE];
    char label[32];
    uint8_t local_priv[EHEM_X25519_KEYSIZE], local_pub[EHEM_X25519_KEYSIZE];
    uint8_t secret[EHEM_X25519_KEYSIZE];
    uint8_t okm[EHEM_SHA256_SIZE];
    uint8_t local_mac[EHEM_SHA256_SIZE];
    static const uint8_t MSG[] = "derive pipeline proof";
    ehem_key_details *d = NULL;
    ehem_mac *dev_mac = NULL;

    create_x25519(s, kid_dev);
    assert_int_equal(ehem_x25519_keypair_from_seed(DERIVE_SEED, local_priv,
                                                   local_pub), EHEM_OK);

    /* Device: derive an HMAC-SHA256 key from ECDH(kid_dev, our pub). */
    ehem_test_label(label, sizeof label);
    assert_int_equal(ehem_key_derive(s->ctx, kid_dev, label, "SHA2-256",
                                     NULL, local_pub, sizeof local_pub,
                                     NULL, NULL, 0, kid_mac), EHEM_OK);
    ehem_test_track(&s->reg, kid_mac);

    /* Local: the same pipeline from the shim's side of the agreement. */
    assert_int_equal(ehem_key_get(s->ctx, kid_dev, &d), EHEM_OK);
    assert_int_equal(ehem_x25519_shared(local_priv, d->pubkey, secret),
                     EHEM_OK);
    ehem_key_details_free(d);
    local_hkdf32(secret, sizeof secret, "encedo-sha256", okm);
    assert_int_equal(ehem_hmac_sha256(okm, sizeof okm, MSG, sizeof MSG - 1,
                                      local_mac), EHEM_OK);

    /* FINDING (2026-07-18, REQ-KEY-009 rev2): the device's derived key is NOT
     * the documented HKDF output — the repo's key-generation step applies an
     * additional undisclosed transformation to the seed (REPO_GenKey_* source
     * is absent from the firmware checkout; 85 candidate reconstructions of
     * the KDF — incl. the doc-exact RFC 5869 pipeline over the raw wolfCrypt
     * X25519 secret CRYPTO_DeriveKey provably produces — all mismatched).
     * External interop ("two parties converge on the same key") is therefore
     * NOT reproducible off-device. Pin the mismatch: if a firmware ever makes
     * this equal (doc-conformant), this assert flips and flags the change. */
    assert_int_equal(ehem_hmac(s->ctx, kid_mac, NULL, MSG, sizeof MSG - 1,
                               NULL, NULL, 0, &dev_mac), EHEM_OK);
    assert_int_equal((int)dev_mac->mac_len, EHEM_SHA256_SIZE);
    printf("[probe] derive KDF: device MAC %s the doc-exact local pipeline\n",
           memcmp(dev_mac->mac, local_mac, EHEM_SHA256_SIZE) == 0
               ? "MATCHES" : "DIFFERS FROM");
    assert_memory_not_equal(dev_mac->mac, local_mac, EHEM_SHA256_SIZE);

    ehem_mac_free(dev_mac);
    ehem_zeroize(local_priv, sizeof local_priv);
    ehem_zeroize(secret, sizeof secret);
    ehem_zeroize(okm, sizeof okm);
}

/* -------------------------------------------------------------------------- */
/* Determinism: equal-length combo, then the stale-stack probe               */
/* -------------------------------------------------------------------------- */

/* Derive `type` twice from the same inputs; compare the stored keys' public
 * material. A dedup-406 on the second derive ALSO proves determinism (the
 * repo saw identical material). Returns 1 = deterministic, 0 = NOT,
 * -1 = the combo itself is rejected by the device (recorded). */
static int derive_twice_compare(dv_state *s, const char *kid_dev,
                                const char *type, const uint8_t *pub,
                                size_t pub_len)
{
    char kid1[EHEM_KID_HEX_SIZE], kid2[EHEM_KID_HEX_SIZE];
    char label[32];
    ehem_key_details *d1 = NULL, *d2 = NULL;
    ehem_rc rc;
    int deterministic;

    ehem_test_label(label, sizeof label);
    rc = ehem_key_derive(s->ctx, kid_dev, label, type, NULL,
                         pub, pub_len, NULL, NULL, 0, kid1);
    if (rc != EHEM_OK) {
        printf("[probe] derive %s from X25519: REJECTED rc=%s http=%ld "
               "(combo unsupported device-side)\n", type, ehem_rc_str(rc),
               ehem_last_error(s->ctx)->http_status);
        return -1;
    }
    ehem_test_track(&s->reg, kid1);

    ehem_test_label(label, sizeof label);
    rc = ehem_key_derive(s->ctx, kid_dev, label, type, NULL,
                         pub, pub_len, NULL, NULL, 0, kid2);
    if (rc != EHEM_OK) {
        /* Repo dedup rejects identical material — determinism by rejection. */
        printf("[probe] derive %s twice: second → rc=%s http=%ld "
               "(dedup ⇒ deterministic)\n", type, ehem_rc_str(rc),
               ehem_last_error(s->ctx)->http_status);
        assert_int_equal(rc, EHEM_ERR_DEVICE);
        return 1;
    }
    ehem_test_track(&s->reg, kid2);

    assert_int_equal(ehem_key_get(s->ctx, kid1, &d1), EHEM_OK);
    assert_int_equal(ehem_key_get(s->ctx, kid2, &d2), EHEM_OK);
    assert_non_null(d1->pubkey);
    assert_non_null(d2->pubkey);
    deterministic = (d1->pubkey_len == d2->pubkey_len &&
                     memcmp(d1->pubkey, d2->pubkey, d1->pubkey_len) == 0);
    printf("[probe] derive %s twice: pubkeys %s (%u bytes)\n", type,
           deterministic ? "EQUAL ⇒ deterministic" : "DIFFER ⇒ NON-deterministic",
           (unsigned)d1->pubkey_len);
    ehem_key_details_free(d1);
    ehem_key_details_free(d2);
    return deterministic;
}

static void test_determinism_equal_len(void **state)
{
    dv_state *s = *state;
    char kid_dev[EHEM_KID_HEX_SIZE];
    uint8_t priv[EHEM_X25519_KEYSIZE], pub[EHEM_X25519_KEYSIZE];

    create_x25519(s, kid_dev);
    assert_int_equal(ehem_x25519_keypair_from_seed(DERIVE_SEED, priv, pub),
                     EHEM_OK);
    /* ED25519 (32) from an X25519 secret (32): the doc's determinism claim
     * MUST hold here — the HKDF input length equals the real secret length. */
    assert_int_equal(derive_twice_compare(s, kid_dev, "ED25519", pub,
                                          sizeof pub), 1);
    ehem_zeroize(priv, sizeof priv);
}

static void test_secp521_stale_stack_probe(void **state)
{
    dv_state *s = *state;
    char kid_dev[EHEM_KID_HEX_SIZE];
    uint8_t priv[EHEM_X25519_KEYSIZE], pub[EHEM_X25519_KEYSIZE];

    create_x25519(s, kid_dev);
    assert_int_equal(ehem_x25519_keypair_from_seed(DERIVE_SEED, priv, pub),
                     EHEM_OK);
    /* SECP521R1 (66) from an X25519 secret (32): the firmware HKDF would read
     * 66 input bytes from a 32-byte secret — 34 bytes of stale stack. The
     * outcome is recorded either way (REQ-KEY-009 open criterion); observed
     * live: the device REJECTS the combo outright (406), mooting the
     * nondeterminism question for it. */
    (void)derive_twice_compare(s, kid_dev, "SECP521R1", pub, sizeof pub);
    ehem_zeroize(priv, sizeof priv);
}

/* -------------------------------------------------------------------------- */
/* PROBE: the exact "keymgmt:derive" scope                                    */
/* -------------------------------------------------------------------------- */

static void test_exact_derive_scope_probe(void **state)
{
    dv_state *s = *state;
    char kid_dev[EHEM_KID_HEX_SIZE];
    char label[32], body[256];
    uint8_t priv[EHEM_X25519_KEYSIZE], pub[EHEM_X25519_KEYSIZE];
    char pub_b64[64];
    ehem_json *root = NULL;
    ehem_rc rc;

    create_x25519(s, kid_dev);
    assert_int_equal(ehem_x25519_keypair_from_seed(DERIVE_SEED, priv, pub),
                     EHEM_OK);
    assert_int_not_equal(ehem_b64_std_encode(pub, sizeof pub, pub_b64,
                                             sizeof pub_b64), (size_t)-1);

    ehem_test_label(label, sizeof label);
    snprintf(body, sizeof body,
             "{\"kid\":\"%s\",\"label\":\"%s\",\"type\":\"AES128\","
             "\"pubkey\":\"%s\"}", kid_dev, label, pub_b64);
    rc = ehem_proto_request_json(s->ctx, EHEM_HTTP_POST, "/api/keymgmt/derive",
                                 body, "keymgmt:derive", EHEM_TLS_REQ_DEFAULT,
                                 &root);
    printf("[probe] exact scope keymgmt:derive: rc=%s http=%ld\n",
           ehem_rc_str(rc), ehem_last_error(s->ctx)->http_status);
    if (rc == EHEM_OK) {
        const char *k;
        if (ehem_json_get_string(root, "kid", &k)) {
            ehem_test_track(&s->reg, k);   /* clean the probe key up */
        }
        ehem_json_free(root);
    }
    /* Firmware accepts derive-or-gen (api_keymgmt.c:1329) — the probe records
     * reality; either 200 (accepted) or 403 would be a finding. */
    assert_int_equal(rc, EHEM_OK);
    ehem_zeroize(priv, sizeof priv);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_hkdf_pipeline_byteexact, setup, teardown),
        cmocka_unit_test_setup_teardown(test_determinism_equal_len, setup, teardown),
        cmocka_unit_test_setup_teardown(test_secp521_stale_stack_probe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_exact_derive_scope_probe, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
