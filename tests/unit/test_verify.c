/*
 * test_verify.c — the exdsa verify binding (src/proto_crypto.c) driven offline
 * through the fake transport.
 *
 * verifies: REQ-OPS-003 (body is exactly {kid,msg(b64),sign(b64),alg[,ctx]};
 *           empty-body 200 → EHEM_OK; a prior ehem_sign token for the same kid
 *           is REUSED (no second acquisition); ARG pre-validation with zero
 *           transport calls; 403 → SCOPE_DENIED, 400/406 → EHEM_ERR_DEVICE —
 *           406 deliberately NOT NOT_FOUND)
 *
 * Helpers mirror test_sign.c (login exchange queued per authenticated call).
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

/* Decode the base64url payload segment of an eJWT (mirrors test_sign.c). */
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

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"

static const uint8_t MSG3[3] = { 0x01, 0x02, 0x03 };     /* b64 "AQID" */
static const uint8_t SIG4[4] = { 0x04, 0x05, 0x0b, 0x0c }; /* b64 "BAULDA==" */

/* -------------------------------------------------------------------------- */
/* request body + empty-200 = valid                                            */
/* -------------------------------------------------------------------------- */

static void test_verify_body_and_ok(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "vf");
    /* The device answers "valid" with an EMPTY 200 body (NULL in the fake). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    assert_int_equal(ehem_verify(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519,
                                 MSG3, sizeof MSG3, NULL, 0,
                                 SIG4, sizeof SIG4), EHEM_OK);
    /* Success must leave no stale last-error from the empty-body detour. */
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);

    assert_int_equal((int)fake_transport_request_count(fake), 3);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_int_equal(r->method, EHEM_HTTP_POST);
    assert_string_equal(r->path, "/api/crypto/exdsa/verify");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"sign\":\"BAULDA==\",\"alg\":\"Ed25519\"}");
    assert_token_scope(fake, 1, "keymgmt:use:" TEST_KID);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_verify_with_sig_ctx_and_body_200(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "vc");
    /* Tolerate a non-empty 200 body too (forward-compat). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ok\":1}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    const uint8_t sctx[2] = { 0x66, 0x6f };              /* b64 "Zm8=" */

    assert_int_equal(ehem_verify(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519CTX,
                                 MSG3, sizeof MSG3, sctx, sizeof sctx,
                                 SIG4, sizeof SIG4), EHEM_OK);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"sign\":\"BAULDA==\",\"alg\":\"Ed25519ctx\","
                        "\"ctx\":\"Zm8=\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* per-kid token shared with ehem_sign                                         */
/* -------------------------------------------------------------------------- */

static void test_verify_shares_sign_token(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "vs");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"sign\":\"BAULDA==\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_signature *sig = NULL;
    assert_int_equal(ehem_sign(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519,
                               MSG3, sizeof MSG3, NULL, 0, &sig), EHEM_OK);
    assert_int_equal(ehem_verify(ctx, TEST_KID, EHEM_SIGN_ALG_ED25519,
                                 MSG3, sizeof MSG3, NULL, 0,
                                 sig->sig, sig->sig_len), EHEM_OK);
    ehem_signature_free(sig);

    /* 4 requests: ONE token acquisition served both sign and verify. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_token_scope(fake, 1, "keymgmt:use:" TEST_KID);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O) + device error mapping                              */
/* -------------------------------------------------------------------------- */

static void test_verify_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    static uint8_t big[EHEM_SIGN_MSG_MAX + 1];
    static uint8_t big_sig[EHEM_VERIFY_SIG_MAX + 1];
    static uint8_t big_ctx[EHEM_SIGN_SIG_CTX_MAX + 1];

    assert_int_equal(ehem_verify(NULL, TEST_KID, "Ed25519", MSG3, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, NULL, "Ed25519", MSG3, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, "not-a-kid", "Ed25519", MSG3, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, NULL, MSG3, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "", MSG3, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", NULL, 3, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 0, NULL, 0,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", big, sizeof big,
                                 NULL, 0, SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, NULL, 0,
                                 NULL, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, NULL, 0,
                                 SIG4, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, NULL, 0,
                                 big_sig, sizeof big_sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, NULL, 5,
                                 SIG4, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, big_ctx,
                                 sizeof big_ctx, SIG4, 4), EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void expect_device_status(ehem_rc want_rc, long status, const char *body)
{
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ve");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    assert_int_equal(ehem_verify(ctx, TEST_KID, "Ed25519", MSG3, 3, NULL, 0,
                                 SIG4, 4), want_rc);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_verify_device_errors(void **state)
{
    (void)state;
    /* 403 (scope not exact / sub == M) → SCOPE_DENIED. */
    expect_device_status(EHEM_ERR_SCOPE_DENIED, 403, "{\"error\":\"scope\"}");
    /* 400 (malformed field / unknown alg) → DEVICE. */
    expect_device_status(EHEM_ERR_DEVICE, 400, "{\"error\":\"alg\"}");
    /* 406: invalid signature / wrong key type / kid not found — the device
     * makes them indistinguishable → DEVICE, NOT NOT_FOUND (REQ-OPS-003). */
    expect_device_status(EHEM_ERR_DEVICE, 406, NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_verify_body_and_ok),
        cmocka_unit_test(test_verify_with_sig_ctx_and_body_200),
        cmocka_unit_test(test_verify_shares_sign_token),
        cmocka_unit_test(test_verify_arg_guards),
        cmocka_unit_test(test_verify_device_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
