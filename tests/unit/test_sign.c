/*
 * test_sign.c — the exdsa sign binding (src/proto_crypto.c) driven offline
 * through the fake transport.
 *
 * verifies: REQ-OPS-001 (body is exactly {kid,msg(b64),alg[,ctx(b64)]}; the
 *           declared scope is the EXACT per-kid keymgmt:use:<kid> and a prior
 *           ehem_key_get token for the same kid is REUSED (no second
 *           acquisition) while a different kid acquires independently; sign
 *           decoded from padded base64 into caller-owned bytes; ARG
 *           pre-validation with zero transport calls; 403 → SCOPE_DENIED,
 *           400/406 → EHEM_ERR_DEVICE (406 deliberately NOT NOT_FOUND);
 *           _free NULL-safe)
 *
 * Every call is authenticated, so it is preceded by a login exchange queued
 * via push_login(); the helpers mirror test_keymgmt.c.
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
#include "ehem/keymgmt.h"
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "ejwt.h"             /* internal: base64url decoder for scope asserts */
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

/* Decode the base64url payload segment of an eJWT (mirrors test_keymgmt.c). */
static void decode_payload(const char *ejwt, char *buf, size_t buf_cap)
{
    const char *d1 = strchr(ejwt, '.');
    const char *d2;
    uint8_t raw[512];
    size_t n;
    assert_non_null(d1);
    d2 = strchr(d1 + 1, '.');
    assert_non_null(d2);
    n = ehem_b64url_decode(d1 + 1, (size_t)(d2 - (d1 + 1)), raw, sizeof raw);
    assert_int_not_equal(n, (size_t)-1);
    assert_true(n < buf_cap);
    memcpy(buf, raw, n);
    buf[n] = '\0';
}

/* Assert the token-acquisition POST at request `token_req` requested `scope`. */
static void assert_token_scope(ehem_transport *fake, size_t token_req,
                               const char *scope)
{
    const fake_captured_request *r = fake_transport_request(fake, token_req);
    const char *q;
    const char *end;
    char ejwt[700];
    char payload[512];
    char needle[80];
    size_t len;

    assert_non_null(r);
    assert_non_null(r->body);
    q = strstr((const char *)r->body, "\"auth\":\"");
    assert_non_null(q);
    q += 8;
    end = strchr(q, '"');
    assert_non_null(end);
    len = (size_t)(end - q);
    assert_true(len < sizeof ejwt);
    memcpy(ejwt, q, len);
    ejwt[len] = '\0';

    decode_payload(ejwt, payload, sizeof payload);
    snprintf(needle, sizeof needle, "\"scope\":\"%s\"", scope);
    assert_non_null(strstr(payload, needle));
}

#define TEST_KID  "09bd0958e1499ecfd51ea62a3f49a84c"
#define TEST_KID2 "aabbccddeeff00112233445566778899"

/* "BAULDA==" is std base64 of {0x04,0x05,0x0b,0x0c}. */
static const char SIGN_RESP[] = "{\"sign\":\"BAULDA==\",\"junk\":1}";
static const uint8_t SIGN_RAW[4] = { 0x04, 0x05, 0x0b, 0x0c };

/* -------------------------------------------------------------------------- */
/* request body + response decode                                              */
/* -------------------------------------------------------------------------- */

static void test_sign_body_and_decode(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "sg");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  SIGN_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_signature *sig = NULL;
    const uint8_t msg[3] = { 0x01, 0x02, 0x03 };   /* b64 "AQID" */

    assert_int_equal(ehem_sign(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519,
                               msg, sizeof msg, NULL, 0, &sig), EHEM_OK);
    assert_non_null(sig);
    assert_int_equal((int)sig->sig_len, 4);
    assert_memory_equal(sig->sig, SIGN_RAW, 4);

    /* Requests: 0 challenge GET, 1 token POST, 2 the sign POST. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_int_equal(r->method, EHEM_HTTP_POST);
    assert_string_equal(r->path, "/api/crypto/exdsa/sign");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"Ed25519\"}");
    assert_token_scope(fake, 1, "keymgmt:use:" TEST_KID);

    ehem_signature_free(sig);
    ehem_signature_free(NULL);        /* NULL-safe */
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_sign_with_sig_ctx(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "sc");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  SIGN_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_signature *sig = NULL;
    const uint8_t msg[1] = { 0x72 };               /* b64 "cg==" */
    const uint8_t sctx[2] = { 0x66, 0x6f };        /* b64 "Zm8=" */

    assert_int_equal(ehem_sign(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519CTX,
                               msg, sizeof msg, sctx, sizeof sctx, &sig),
                     EHEM_OK);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"cg==\","
                        "\"alg\":\"Ed25519ctx\",\"ctx\":\"Zm8=\"}");

    ehem_signature_free(sig);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* per-kid token shared with ehem_key_get                                      */
/* -------------------------------------------------------------------------- */

static const char GET_RESP[] =
    "{\"type\":\"ED25519\",\"pubkey\":\"BAULDA==\",\"updated\":1000}";

static void test_sign_shares_get_token(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* Script: login (2) + get + sign — the sign must NOT re-login — then a
     * second kid: login (2) + sign (its own scope). */
    push_login(fake, "k1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  GET_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  SIGN_RESP), 0);
    push_login(fake, "k2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  SIGN_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    const uint8_t msg[3] = { 1, 2, 3 };

    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, TEST_KID, &d), EHEM_OK);
    ehem_key_details_free(d);

    ehem_signature *sig = NULL;
    assert_int_equal(ehem_sign(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519,
                               msg, sizeof msg, NULL, 0, &sig), EHEM_OK);
    ehem_signature_free(sig);

    /* 4 requests so far: ONE token acquisition served both get and sign. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_token_scope(fake, 1, "keymgmt:use:" TEST_KID);

    /* A different kid is a different scope: its own acquisition. */
    sig = NULL;
    assert_int_equal(ehem_sign(ctx, TEST_KID2, EHEM_SIGN_ALG_ED25519,
                               msg, sizeof msg, NULL, 0, &sig), EHEM_OK);
    ehem_signature_free(sig);
    assert_int_equal((int)fake_transport_request_count(fake), 7);
    /* Requests 4/5 are the second login (challenge GET + token POST). */
    assert_token_scope(fake, 5, "keymgmt:use:" TEST_KID2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O) + device error mapping                              */
/* -------------------------------------------------------------------------- */

static void test_sign_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_signature *sig = NULL;
    const uint8_t msg[1] = { 0x41 };
    static uint8_t big[EHEM_SIGN_MSG_MAX + 1];
    static uint8_t big_ctx[EHEM_SIGN_SIG_CTX_MAX + 1];

    assert_int_equal(ehem_sign(NULL, TEST_KID, "Ed25519", msg, 1, NULL, 0,
                               &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, NULL, "Ed25519", msg, 1, NULL, 0, &sig),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, "not-a-kid", "Ed25519", msg, 1, NULL, 0,
                               &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, NULL, msg, 1, NULL, 0, &sig),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "", msg, 1, NULL, 0, &sig),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", NULL, 1, NULL, 0,
                               &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 0, NULL, 0,
                               &sig), EHEM_ERR_ARG);       /* device rejects empty */
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", big, sizeof big,
                               NULL, 0, &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, NULL, 5,
                               &sig), EHEM_ERR_ARG);       /* NULL ctx, len > 0 */
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, big_ctx,
                               sizeof big_ctx, &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, NULL, 0,
                               NULL), EHEM_ERR_ARG);

    /* None of the above touched the transport. */
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_null(sig);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* One scripted device error per call; each asserts rc + recorded status. */
static void expect_device_status(ehem_rc want_rc, long status, const char *body)
{
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "er");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_signature *sig = NULL;
    const uint8_t msg[1] = { 0x41 };

    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, NULL, 0,
                               &sig), want_rc);
    assert_null(sig);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_sign_device_errors(void **state)
{
    (void)state;
    /* 403 (scope not exact / sub == M) → SCOPE_DENIED. */
    expect_device_status(EHEM_ERR_SCOPE_DENIED, 403, "{\"error\":\"scope\"}");
    /* 400 (malformed field / unknown alg) → DEVICE with payload. */
    expect_device_status(EHEM_ERR_DEVICE, 400, "{\"error\":\"alg\"}");
    /* 406 is ambiguous on this endpoint (kid not found / wrong key type /
     * crypto failure) → DEVICE, deliberately NOT NOT_FOUND (REQ-OPS-001). */
    expect_device_status(EHEM_ERR_DEVICE, 406, NULL);
}

static void test_sign_protocol_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "pr");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"nosign\":1}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"sign\":\"@@bad@@\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_signature *sig = NULL;
    const uint8_t msg[1] = { 0x41 };

    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, NULL, 0,
                               &sig), EHEM_ERR_PROTOCOL);
    assert_null(sig);
    assert_int_equal(ehem_sign(ctx, TEST_KID, "Ed25519", msg, 1, NULL, 0,
                               &sig), EHEM_ERR_PROTOCOL);
    assert_null(sig);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sign_body_and_decode),
        cmocka_unit_test(test_sign_with_sig_ctx),
        cmocka_unit_test(test_sign_shares_get_token),
        cmocka_unit_test(test_sign_arg_guards),
        cmocka_unit_test(test_sign_device_errors),
        cmocka_unit_test(test_sign_protocol_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
