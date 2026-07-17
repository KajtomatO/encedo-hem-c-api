/*
 * test_cipher.c — the cipher encrypt/decrypt bindings (src/proto_crypto.c)
 * driven offline through the fake transport.
 *
 * verifies: REQ-OPS-006 (encrypt body carries {kid,msg(b64),alg} + aad/
 *           ext_kid/pubkey/ctx only when given and NEVER an iv field;
 *           decrypt body adds iv/tag when given; ciphertext/iv/tag/plaintext
 *           decoded into caller-owned buffers with the ECB no-iv shape
 *           preserved; EHEM_ERR_ARG guards [alg length, aad > 16, iv/tag ≠
 *           16, hkdf_ctx > 64, msg bounds] with zero I/O; 403 →
 *           SCOPE_DENIED, 400/406 → EHEM_ERR_DEVICE; encrypt+decrypt share
 *           ONE per-KID token)
 *
 * Helpers mirror test_sign.c / test_hmac.c.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push_login(ehem_transport *fake, const char *tag)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                 (long long)(EJWT_FX_NOW + 100000), tag);
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

static ehem_ctx *logged_in_ctx(ehem_transport *fake)
{
    ehem_ctx *ctx = ctx_with(fake);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    return ctx;
}

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"
#define PEER_KID "aabbccddeeff00112233445566778899"

static const uint8_t MSG3[3] = { 0x01, 0x02, 0x03 };   /* b64 "AQID" */
static const uint8_t AAD2[2] = { 0x61, 0x62 };         /* b64 "YWI=" */
static const uint8_t CTX3[3] = { 0x63, 0x74, 0x78 };   /* b64 "Y3R4" */

/* 16 bytes 0x00..0x0f: b64 "AAECAwQFBgcICQoLDA0ODw==". */
static const uint8_t IV16[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
#define IV16_B64 "AAECAwQFBgcICQoLDA0ODw=="

/* GCM-shaped response: ciphertext {4,5,11,12}, iv = IV16, tag = IV16. */
static const char GCM_RESP[] =
    "{\"ciphertext\":\"BAULDA==\",\"iv\":\"" IV16_B64 "\","
    "\"tag\":\"" IV16_B64 "\"}";
/* ECB-shaped response: ciphertext only. */
static const char ECB_RESP[] = "{\"ciphertext\":\"BAULDA==\"}";
static const uint8_t CT_RAW[4] = { 0x04, 0x05, 0x0b, 0x0c };

/* -------------------------------------------------------------------------- */
/* encrypt: bodies + response shapes                                           */
/* -------------------------------------------------------------------------- */

static void test_encrypt_gcm_body_and_shape(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "c1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  GCM_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ciphertext *ct = NULL;

    assert_int_equal(ehem_encrypt(ctx, TEST_KID, EHEM_CIPHER_ALG_AES256_GCM,
                                  MSG3, sizeof MSG3, AAD2, sizeof AAD2,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    assert_non_null(ct);
    assert_int_equal((int)ct->ciphertext_len, 4);
    assert_memory_equal(ct->ciphertext, CT_RAW, 4);
    assert_true(ct->has_iv);
    assert_memory_equal(ct->iv, IV16, 16);
    assert_true(ct->has_tag);
    assert_memory_equal(ct->tag, IV16, 16);

    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_int_equal(r->method, EHEM_HTTP_POST);
    assert_string_equal(r->path, "/api/crypto/cipher/encrypt");
    /* No iv field EVER on encrypt (device generates it). */
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"AES256-GCM\",\"aad\":\"YWI=\"}");

    ehem_ciphertext_free(ct);
    ehem_ciphertext_free(NULL);       /* NULL-safe */
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_encrypt_ecb_shape_and_derived_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "c2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  ECB_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  GCM_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ciphertext *ct = NULL;

    /* ECB: no iv/tag in the response — shape preserved. */
    static const uint8_t block16[16] = { 0 };
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, EHEM_CIPHER_ALG_AES128_ECB,
                                  block16, sizeof block16, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    assert_false(ct->has_iv);
    assert_false(ct->has_tag);
    ehem_ciphertext_free(ct);
    ct = NULL;

    /* ECDH-derived flow: pubkey + hkdf ctx ride the body. */
    static const uint8_t pub3[3] = { 0x0a, 0x0b, 0x0c };   /* "CgsM" */
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, EHEM_CIPHER_ALG_AES256_GCM,
                                  MSG3, sizeof MSG3, NULL, 0,
                                  NULL, pub3, sizeof pub3,
                                  CTX3, sizeof CTX3, &ct), EHEM_OK);
    ehem_ciphertext_free(ct);
    const fake_captured_request *r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"AES256-GCM\",\"pubkey\":\"CgsM\","
                        "\"ctx\":\"Y3R4\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* decrypt: body + token sharing                                               */
/* -------------------------------------------------------------------------- */

static void test_decrypt_body_and_token_share(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "c3");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  GCM_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"plaintext\":\"AQID\"}"),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ciphertext *ct = NULL;
    ehem_plaintext *pt = NULL;

    assert_int_equal(ehem_encrypt(ctx, TEST_KID, EHEM_CIPHER_ALG_AES256_GCM,
                                  MSG3, sizeof MSG3, AAD2, sizeof AAD2,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_OK);
    assert_int_equal(ehem_decrypt(ctx, TEST_KID, EHEM_CIPHER_ALG_AES256_GCM,
                                  ct->ciphertext, ct->ciphertext_len,
                                  ct->iv, sizeof ct->iv,
                                  ct->tag, sizeof ct->tag,
                                  AAD2, sizeof AAD2,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_OK);
    assert_non_null(pt);
    assert_int_equal((int)pt->plaintext_len, 3);
    assert_memory_equal(pt->plaintext, MSG3, 3);

    /* 4 requests: ONE token acquisition served encrypt and decrypt. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    const fake_captured_request *r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/cipher/decrypt");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"BAULDA==\","
                        "\"alg\":\"AES256-GCM\",\"iv\":\"" IV16_B64 "\","
                        "\"tag\":\"" IV16_B64 "\",\"aad\":\"YWI=\"}");

    ehem_ciphertext_free(ct);
    ehem_plaintext_free(pt);
    ehem_plaintext_free(NULL);        /* NULL-safe */
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O)                                                     */
/* -------------------------------------------------------------------------- */

static void test_cipher_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_ciphertext *ct = NULL;
    ehem_plaintext *pt = NULL;
    static uint8_t big[EHEM_CIPHER_CT_MAX + 1];
    static uint8_t big_aad[EHEM_CIPHER_AAD_MAX + 1];
    static uint8_t big_hctx[EHEM_CIPHER_HKDF_CTX_MAX + 1];
    uint8_t iv15[15] = { 0 };

    assert_int_equal(ehem_encrypt(NULL, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, "nope", "AES256-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    /* alg must be exactly 10 chars (device rule pre-validated). */
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM!", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", NULL, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 0,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", big,
                                  EHEM_CIPHER_MSG_MAX + 1, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &ct), EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  big_aad, sizeof big_aad, NULL, NULL, 0,
                                  NULL, 0, &ct), EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 2, NULL, NULL, 0, NULL, 0, &ct),
                     EHEM_ERR_ARG);   /* NULL aad, nonzero len */
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 0, PEER_KID, MSG3, 3, NULL, 0, &ct),
                     EHEM_ERR_ARG);   /* both peers */
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, big_hctx,
                                  sizeof big_hctx, &ct), EHEM_ERR_ARG);
    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, NULL),
                     EHEM_ERR_ARG);

    /* Decrypt-specific: iv/tag exact-16 rule; ct bound is 2064. */
    assert_int_equal(ehem_decrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  iv15, sizeof iv15, NULL, 0, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_ERR_ARG);
    assert_int_equal(ehem_decrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  IV16, 16, iv15, sizeof iv15, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_ERR_ARG);
    assert_int_equal(ehem_decrypt(ctx, TEST_KID, "AES256-GCM", NULL, 16,
                                  IV16, 16, NULL, 0, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_ERR_ARG);
    assert_int_equal(ehem_decrypt(ctx, TEST_KID, "AES256-GCM", big,
                                  sizeof big, IV16, 16, NULL, 0, NULL, 0,
                                  NULL, NULL, 0, NULL, 0, &pt), EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_null(ct);
    assert_null(pt);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* device error mapping                                                        */
/* -------------------------------------------------------------------------- */

static void expect_device_status(ehem_rc want_rc, long status, const char *body)
{
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ce");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ciphertext *ct = NULL;

    assert_int_equal(ehem_encrypt(ctx, TEST_KID, "AES256-GCM", MSG3, 3,
                                  NULL, 0, NULL, NULL, 0, NULL, 0, &ct),
                     want_rc);
    assert_null(ct);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_cipher_device_errors(void **state)
{
    (void)state;
    expect_device_status(EHEM_ERR_SCOPE_DENIED, 403, "{\"error\":\"scope\"}");
    /* 400: bad alg literal / ECB not block-aligned. */
    expect_device_status(EHEM_ERR_DEVICE, 400, "{\"error\":\"alg\"}");
    /* 406: key/width mismatch, GCM tag mismatch (decrypt), ECDH failure. */
    expect_device_status(EHEM_ERR_DEVICE, 406, NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encrypt_gcm_body_and_shape),
        cmocka_unit_test(test_encrypt_ecb_shape_and_derived_body),
        cmocka_unit_test(test_decrypt_body_and_token_share),
        cmocka_unit_test(test_cipher_arg_guards),
        cmocka_unit_test(test_cipher_device_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
