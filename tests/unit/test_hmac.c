/*
 * test_hmac.c — the hmac hash/verify bindings (src/proto_crypto.c) driven
 * offline through the fake transport.
 *
 * verifies: REQ-OPS-005 (hash body is {kid,msg(b64)} + alg/ext_kid/pubkey
 *           only when given; verify body adds mac(b64); mac decoded into a
 *           caller-owned buffer; empty-200 verify → EHEM_OK; derived flow
 *           without alg → EHEM_ERR_ARG with zero I/O; mac/msg bounds and
 *           peer violations → EHEM_ERR_ARG; 403 → SCOPE_DENIED, 400/406 →
 *           EHEM_ERR_DEVICE; hash+verify share ONE per-KID token)
 *
 * Helpers mirror test_sign.c / test_ecdh.c.
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

static const char MAC_RESP[] = "{\"mac\":\"BAULDA==\"}";
static const uint8_t MAC_RAW[4] = { 0x04, 0x05, 0x0b, 0x0c };
static const uint8_t MSG3[3] = { 0x01, 0x02, 0x03 };   /* b64 "AQID" */
static const uint8_t PUB3[3] = { 0x0a, 0x0b, 0x0c };   /* b64 "CgsM" */

/* -------------------------------------------------------------------------- */
/* hash: body variants + decode                                                */
/* -------------------------------------------------------------------------- */

static void test_hmac_direct_body_no_alg(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "h1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  MAC_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mac *m = NULL;

    /* Direct flow: alg NULL (the key's own type decides device-side). */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, sizeof MSG3,
                               NULL, NULL, 0, &m), EHEM_OK);
    assert_non_null(m);
    assert_int_equal((int)m->mac_len, 4);
    assert_memory_equal(m->mac, MAC_RAW, 4);

    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_int_equal(r->method, EHEM_HTTP_POST);
    assert_string_equal(r->path, "/api/crypto/hmac/hash");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\"}");

    ehem_mac_free(m);
    ehem_mac_free(NULL);              /* NULL-safe */
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_hmac_derived_bodies(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "h2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  MAC_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  MAC_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mac *m = NULL;

    /* ext_kid + alg. */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, EHEM_HASH_ALG_SHA2_256,
                               MSG3, sizeof MSG3, PEER_KID, NULL, 0, &m),
                     EHEM_OK);
    ehem_mac_free(m);
    m = NULL;
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"SHA2-256\",\"ext_kid\":\"" PEER_KID "\"}");

    /* pubkey + alg. */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, EHEM_HASH_ALG_SHA3_512,
                               MSG3, sizeof MSG3, NULL, PUB3, sizeof PUB3, &m),
                     EHEM_OK);
    ehem_mac_free(m);
    r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"SHA3-512\",\"pubkey\":\"CgsM\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* verify: body + empty-200 + token sharing                                    */
/* -------------------------------------------------------------------------- */

static void test_hmac_verify_ok_and_token_share(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "h3");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  MAC_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mac *m = NULL;

    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, sizeof MSG3,
                               NULL, NULL, 0, &m), EHEM_OK);
    assert_int_equal(ehem_hmac_verify(ctx, TEST_KID, NULL, MSG3, sizeof MSG3,
                                      m->mac, m->mac_len, NULL, NULL, 0),
                     EHEM_OK);
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);
    ehem_mac_free(m);

    /* 4 requests: ONE token acquisition served hash and verify. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    const fake_captured_request *r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/hmac/verify");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"mac\":\"BAULDA==\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O)                                                     */
/* -------------------------------------------------------------------------- */

static void test_hmac_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_mac *m = NULL;
    static uint8_t big[EHEM_SIGN_MSG_MAX + 1];
    static uint8_t big_mac[EHEM_HMAC_MAC_MAX + 1];
    uint8_t mac4[4] = { 1, 2, 3, 4 };

    assert_int_equal(ehem_hmac(NULL, TEST_KID, NULL, MSG3, 3, NULL, NULL, 0,
                               &m), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, NULL, NULL, MSG3, 3, NULL, NULL, 0, &m),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, "nope", NULL, MSG3, 3, NULL, NULL, 0, &m),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, NULL, 3, NULL, NULL, 0,
                               &m), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, 0, NULL, NULL, 0,
                               &m), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, big, sizeof big, NULL,
                               NULL, 0, &m), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, 3, NULL, NULL, 0,
                               NULL), EHEM_ERR_ARG);
    /* Empty alg string. */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, "", MSG3, 3, NULL, NULL, 0, &m),
                     EHEM_ERR_ARG);
    /* DERIVED flow without alg (fw would 406 opaquely — REQ-OPS-005). */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, 3, PEER_KID, NULL,
                               0, &m), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, 3, NULL, PUB3,
                               sizeof PUB3, &m), EHEM_ERR_ARG);
    /* Both peers. */
    assert_int_equal(ehem_hmac(ctx, TEST_KID, "SHA2-256", MSG3, 3, PEER_KID,
                               PUB3, sizeof PUB3, &m), EHEM_ERR_ARG);
    /* Verify-specific: NULL mac / bad mac bounds. */
    assert_int_equal(ehem_hmac_verify(ctx, TEST_KID, NULL, MSG3, 3, NULL, 4,
                                      NULL, NULL, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac_verify(ctx, TEST_KID, NULL, MSG3, 3, mac4, 0,
                                      NULL, NULL, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_hmac_verify(ctx, TEST_KID, NULL, MSG3, 3, big_mac,
                                      sizeof big_mac, NULL, NULL, 0),
                     EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_null(m);

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
    push_login(fake, "he");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mac *m = NULL;

    assert_int_equal(ehem_hmac(ctx, TEST_KID, NULL, MSG3, 3, NULL, NULL, 0,
                               &m), want_rc);
    assert_null(m);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_hmac_device_errors(void **state)
{
    (void)state;
    expect_device_status(EHEM_ERR_SCOPE_DENIED, 403, "{\"error\":\"scope\"}");
    expect_device_status(EHEM_ERR_DEVICE, 400, "{\"error\":\"alg\"}");
    /* 406: wrong key type / ECDH failure / (verify) MAC mismatch. */
    expect_device_status(EHEM_ERR_DEVICE, 406, NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hmac_direct_body_no_alg),
        cmocka_unit_test(test_hmac_derived_bodies),
        cmocka_unit_test(test_hmac_verify_ok_and_token_share),
        cmocka_unit_test(test_hmac_arg_guards),
        cmocka_unit_test(test_hmac_device_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
