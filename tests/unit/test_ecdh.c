/*
 * test_ecdh.c — the ECDH binding (src/proto_crypto.c) driven offline through
 * the fake transport.
 *
 * verifies: REQ-OPS-004 (body carries {kid} plus exactly one of {ext_kid}/
 *           {pubkey(b64)} and alg only when given; 'ecdh' decoded into a
 *           caller-owned zeroized-on-free buffer; both-or-neither peer and
 *           oversize pubkey → EHEM_ERR_ARG with zero transport calls; 403 →
 *           SCOPE_DENIED, 400/406 → EHEM_ERR_DEVICE; per-KID token shared
 *           with the other crypto ops)
 *
 * Helpers mirror test_sign.c / test_verify.c.
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
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "ejwt.h"             /* internal: base64url for scope asserts */
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

/* "BAULDA==" = {0x04,0x05,0x0b,0x0c}. */
static const char ECDH_RESP[] = "{\"ecdh\":\"BAULDA==\",\"junk\":1}";
static const uint8_t ECDH_RAW[4] = { 0x04, 0x05, 0x0b, 0x0c };
static const uint8_t PUB3[3] = { 0x01, 0x02, 0x03 };   /* b64 "AQID" */

/* -------------------------------------------------------------------------- */
/* body variants + decode                                                      */
/* -------------------------------------------------------------------------- */

static void test_ecdh_ext_kid_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "e1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  ECDH_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ecdh_secret *sec = NULL;

    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_OK);
    assert_non_null(sec);
    assert_int_equal((int)sec->secret_len, 4);
    assert_memory_equal(sec->secret, ECDH_RAW, 4);

    assert_int_equal((int)fake_transport_request_count(fake), 3);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_int_equal(r->method, EHEM_HTTP_POST);
    assert_string_equal(r->path, "/api/crypto/ecdh");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\","
                        "\"ext_kid\":\"" PEER_KID "\"}");

    ehem_ecdh_secret_free(sec);
    ehem_ecdh_secret_free(NULL);      /* NULL-safe */
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_ecdh_pubkey_and_alg_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "e2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  ECDH_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ecdh_secret *sec = NULL;

    assert_int_equal(ehem_ecdh(ctx, TEST_KID, NULL, PUB3, sizeof PUB3,
                               EHEM_HASH_ALG_SHA2_256, &sec), EHEM_OK);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"pubkey\":\"AQID\","
                        "\"alg\":\"SHA2-256\"}");

    ehem_ecdh_secret_free(sec);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O)                                                     */
/* -------------------------------------------------------------------------- */

static void test_ecdh_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_ecdh_secret *sec = NULL;
    static uint8_t big_pub[EHEM_ECDH_PUBKEY_MAX + 1];

    assert_int_equal(ehem_ecdh(NULL, TEST_KID, PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdh(ctx, NULL, PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdh(ctx, "nope", PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_ERR_ARG);
    /* Neither peer / both peers. */
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, NULL, NULL, 0, NULL, &sec),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, PUB3, sizeof PUB3,
                               NULL, &sec), EHEM_ERR_ARG);
    /* Malformed peer args. */
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, "bad-kid", NULL, 0, NULL, &sec),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, NULL, NULL, 3, NULL, &sec),
                     EHEM_ERR_ARG);   /* NULL pubkey, nonzero len */
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, NULL, PUB3, 0, NULL, &sec),
                     EHEM_ERR_ARG);   /* pubkey given but len 0 → "neither" is
                                       * false (ptr set) and len invalid */
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, NULL, big_pub, sizeof big_pub,
                               NULL, &sec), EHEM_ERR_ARG);
    /* Empty alg string (NULL is the way to omit). */
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, "", &sec),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, NULL, NULL),
                     EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_null(sec);

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
    push_login(fake, "ee");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ecdh_secret *sec = NULL;

    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, NULL, &sec),
                     want_rc);
    assert_null(sec);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_ecdh_device_errors(void **state)
{
    (void)state;
    expect_device_status(EHEM_ERR_SCOPE_DENIED, 403, "{\"error\":\"scope\"}");
    expect_device_status(EHEM_ERR_DEVICE, 400, "{\"error\":\"pubkey\"}");
    /* 406: not found / not ECDH-capable / family mismatch / crypto failure —
     * indistinguishable (REQ-OPS-004). */
    expect_device_status(EHEM_ERR_DEVICE, 406, NULL);
}

static void test_ecdh_protocol_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ep");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"noecdh\":1}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ecdh\":\"@@bad@@\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ecdh_secret *sec = NULL;

    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_ERR_PROTOCOL);
    assert_null(sec);
    assert_int_equal(ehem_ecdh(ctx, TEST_KID, PEER_KID, NULL, 0, NULL, &sec),
                     EHEM_ERR_PROTOCOL);
    assert_null(sec);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ecdh_ext_kid_body),
        cmocka_unit_test(test_ecdh_pubkey_and_alg_body),
        cmocka_unit_test(test_ecdh_arg_guards),
        cmocka_unit_test(test_ecdh_device_errors),
        cmocka_unit_test(test_ecdh_protocol_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
