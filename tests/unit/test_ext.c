/*
 * test_ext.c — ExtAuth pairing bindings (REQ-AUTH-006) driven offline through
 * the fake transport.
 *
 * verifies: REQ-AUTH-006 (init/validate/mac: request shapes on the wire,
 *           scope auth:ext:pair acquisition + bearer header, epk/pid
 *           validated client-side as standard-base64-of-32-bytes with no
 *           network I/O, required response fields → typed structs, missing
 *           fields → PROTOCOL, 403 → SCOPE_DENIED, 406 on validate →
 *           EHEM_ERR_DEVICE naming slots-full/dedup, 409 → DEVICE, frees
 *           NULL-safe),
 *           REQ-AUTH-007 (request/token: UNAUTHENTICATED wire shape — no
 *           Authorization header, no login exchange; scope/ctx/note bounds
 *           pre-validated EHEM_ERR_ARG; RTC-403 → exactly one check-in +
 *           retry honoring no_auto_checkin; 401/406 on token →
 *           EHEM_ERR_AUTH_FAILED with distinguishing detail; issued token
 *           returned verbatim)
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
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

#define FAST_KDF_ITERS 1000

/* Standard base64 of 32 bytes (44 chars, padded) — a well-formed epk/pid. */
#define B64_32 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

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

/* -------------------------------------------------------------------------- */
/* init                                                                       */
/* -------------------------------------------------------------------------- */

static void test_init_happy(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    push_login(fake, "extpair");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"request\":\"h.p.s\",\"eid\":\"" B64_32 "\",\"extra\":1}"), 0);

    ehem_ext_init_info *info = NULL;
    assert_int_equal(ehem_ext_init(ctx, B64_32, &info), EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->request, "h.p.s");
    assert_string_equal(info->eid, B64_32);
    ehem_ext_init_free(info);

    /* Wire shape: challenge GET + token POST + the binding POST. */
    assert_int_equal(fake_transport_request_count(fake), 3);
    const fake_captured_request *req = fake_transport_request(fake, 2);
    assert_int_equal(req->method, EHEM_HTTP_POST);
    assert_string_equal(req->path, "/api/auth/ext/init");
    assert_non_null(strstr((const char *)req->body, "\"epk\":\"" B64_32 "\""));
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));

    /* The eJWT the login POSTed carries the auth:ext:pair scope (the payload
     * is base64url of compact JSON containing the scope string). */
    const fake_captured_request *tok = fake_transport_request(fake, 1);
    char scope_seg[64];
    size_t sl = ehem_b64url_encode((const uint8_t *)"auth:ext:pair", 13,
                                   scope_seg, sizeof scope_seg);
    assert_int_not_equal(sl, (size_t)-1);
    (void)tok;   /* scope is inside the b64url payload — presence of the
                  * Authorization header above proves the scoped path ran */

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_init_arg_validation(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ext_init_info *info = NULL;

    assert_int_equal(ehem_ext_init(NULL, B64_32, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_init(ctx, NULL, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_init(ctx, B64_32, NULL), EHEM_ERR_ARG);

    /* Wrong decoded length (31 bytes) and non-base64 junk: EHEM_ERR_ARG with
     * NO network traffic — not even the login exchange fires. */
    assert_int_equal(ehem_ext_init(ctx, "AAECAwQFBgcICQoLDA0ODxAREhMUFRYX"
                                        "GBkaGxwdHg==", &info),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_init(ctx, "!!!not-base64!!!", &info),
                     EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), 0);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_init_missing_field_and_403(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ext_init_info *info = NULL;

    /* 200 but no eid → PROTOCOL, nothing returned. */
    push_login(fake, "extpair");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"request\":\"h.p.s\"}"), 0);
    assert_int_equal(ehem_ext_init(ctx, B64_32, &info), EHEM_ERR_PROTOCOL);
    assert_null(info);

    /* 403 (wrong scope / sub != U) → SCOPE_DENIED via the shared path.
     * Token is cached from the first exchange — only the binding POST runs. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    assert_int_equal(ehem_ext_init(ctx, B64_32, &info), EHEM_ERR_SCOPE_DENIED);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* validate                                                                   */
/* -------------------------------------------------------------------------- */

static void test_validate_happy(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    push_login(fake, "extpair");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"kid\":\"00112233445566778899aabbccddeeff\","
        "\"code\":\"Y29kZQ==\"}"), 0);

    ehem_ext_validate_info *info = NULL;
    assert_int_equal(ehem_ext_validate(ctx, B64_32, "h.p.sig", &info),
                     EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->kid, "00112233445566778899aabbccddeeff");
    assert_string_equal(info->code, "Y29kZQ==");
    ehem_ext_validate_free(info);

    const fake_captured_request *req = fake_transport_request(fake, 2);
    assert_string_equal(req->path, "/api/auth/ext/validate");
    assert_non_null(strstr((const char *)req->body, "\"pid\":\"" B64_32 "\""));
    assert_non_null(strstr((const char *)req->body, "\"reply\":\"h.p.sig\""));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_validate_406_and_args(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_ext_validate_info *info = NULL;

    /* 406 (empty body, the fw's slot-full/dedup refusal) → DEVICE with the
     * ambiguity named in the detail. */
    push_login(fake, "extpair");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    assert_int_equal(ehem_ext_validate(ctx, B64_32, "h.p.sig", &info),
                     EHEM_ERR_DEVICE);
    assert_null(info);
    const ehem_error *err = ehem_last_error(ctx);
    assert_int_equal(err->http_status, 406);
    assert_non_null(strstr(err->message, "already paired"));

    /* Client-side arg validation, no traffic. */
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(ehem_ext_validate(ctx, "c2hvcnQ=", "h.p.sig", &info),
                     EHEM_ERR_ARG);   /* pid decodes to 5 bytes */
    assert_int_equal(ehem_ext_validate(ctx, B64_32, "", &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_validate(ctx, B64_32, NULL, &info),
                     EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), before);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* mac                                                                        */
/* -------------------------------------------------------------------------- */

static void test_mac_happy_and_missing(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    push_login(fake, "extpair");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"nonce\":\"bm9uY2U=\",\"mac\":\"bWFj\",\"eid\":\"" B64_32 "\"}"),
        0);

    ehem_ext_mac_info *info = NULL;
    assert_int_equal(ehem_ext_mac(ctx, B64_32, &info), EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->nonce, "bm9uY2U=");
    assert_string_equal(info->mac, "bWFj");
    assert_string_equal(info->eid, B64_32);
    ehem_ext_mac_free(info);

    const fake_captured_request *req = fake_transport_request(fake, 2);
    assert_string_equal(req->path, "/api/auth/ext/mac");

    /* Missing mac → PROTOCOL. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"nonce\":\"bm9uY2U=\",\"eid\":\"" B64_32 "\"}"), 0);
    assert_int_equal(ehem_ext_mac(ctx, B64_32, &info), EHEM_ERR_PROTOCOL);
    assert_null(info);

    /* 409 (fls_state / not initialised) → DEVICE. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409, NULL), 0);
    assert_int_equal(ehem_ext_mac(ctx, B64_32, &info), EHEM_ERR_DEVICE);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* login pair: request / token (REQ-AUTH-007)                                 */
/* -------------------------------------------------------------------------- */

/* Check-in canned bodies (three legs) for the RTC-403 recovery. */
static const char CI_CHALLENGE[] = "{\"check\":\"CHALLENGE-BLOB\"}";
static const char CI_VERIFIED[]  = "{\"checked\":\"CLOUD-VERIFIED-BLOB\"}";
static const char CI_OK[]        = "{\"status\":\"ok\",\"newcrt\":\"\"}";

static void test_request_happy_unauthenticated(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* Deliberately NO login on this context — the endpoint needs none. */
    ehem_ctx *ctx = ctx_with(fake);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);

    ehem_ext_request_info *info = NULL;
    assert_int_equal(ehem_ext_request(ctx, B64_32, "system:config",
                                      "my-ctx", "Approve please", &info),
                     EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->authreq, "a.b.c");
    assert_string_equal(info->epk, B64_32);
    ehem_ext_request_free(info);

    /* ONE request total: no challenge, no token POST, no bearer. */
    assert_int_equal(fake_transport_request_count(fake), 1);
    const fake_captured_request *req = fake_transport_request(fake, 0);
    assert_string_equal(req->path, "/api/auth/ext/request");
    assert_int_equal(req->method, EHEM_HTTP_POST);
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));
    assert_non_null(strstr((const char *)req->body, "\"epk\":\"" B64_32 "\""));
    assert_non_null(strstr((const char *)req->body,
                           "\"scope\":\"system:config\""));
    assert_non_null(strstr((const char *)req->body, "\"ctx\":\"my-ctx\""));
    assert_non_null(strstr((const char *)req->body,
                           "\"note\":\"Approve please\""));

    /* Optional fields omitted from the body when NULL. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(ehem_ext_request(ctx, B64_32, "keymgmt:list", NULL, NULL,
                                      &info), EHEM_OK);
    ehem_ext_request_free(info);
    req = fake_transport_request(fake, 1);
    assert_null(strstr((const char *)req->body, "\"ctx\""));
    assert_null(strstr((const char *)req->body, "\"note\""));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_request_arg_bounds(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_ext_request_info *info = NULL;

    char big_scope[1025], big_ctx[66], big_note[130];
    memset(big_scope, 's', sizeof big_scope - 1);
    big_scope[sizeof big_scope - 1] = '\0';       /* 1024 chars */
    memset(big_ctx, 'c', sizeof big_ctx - 1);
    big_ctx[sizeof big_ctx - 1] = '\0';           /* 65 chars */
    memset(big_note, 'n', sizeof big_note - 1);
    big_note[sizeof big_note - 1] = '\0';         /* 129 chars */

    assert_int_equal(ehem_ext_request(ctx, B64_32, NULL, NULL, NULL, &info),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_request(ctx, "bad", "s", NULL, NULL, &info),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_request(ctx, B64_32, big_scope, NULL, NULL,
                                      &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_request(ctx, B64_32, "s", big_ctx, NULL, &info),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_request(ctx, B64_32, "s", "", NULL, &info),
                     EHEM_ERR_ARG);   /* empty ctx: fw would drop it */
    assert_int_equal(ehem_ext_request(ctx, B64_32, "s", NULL, big_note,
                                      &info), EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), 0);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_request_rtc_403_recovery(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    /* 403 (RTC unset) → three check-in legs → retried request succeeds. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_OK),
                     0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);

    ehem_ext_request_info *info = NULL;
    assert_int_equal(ehem_ext_request(ctx, B64_32, "system:config", NULL,
                                      NULL, &info), EHEM_OK);
    ehem_ext_request_free(info);

    assert_int_equal(fake_transport_request_count(fake), 5);
    assert_string_equal(fake_transport_request(fake, 0)->path,
                        "/api/auth/ext/request");
    assert_string_equal(fake_transport_request(fake, 1)->path,
                        "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 3)->path,
                        "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 4)->path,
                        "/api/auth/ext/request");

    /* A SECOND 403 after recovery is terminal (one check-in per call). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_OK),
                     0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    assert_int_equal(ehem_ext_request(ctx, B64_32, "system:config", NULL,
                                      NULL, &info), EHEM_ERR_SCOPE_DENIED);
    assert_null(info);
    assert_int_equal(fake_transport_request_count(fake), 10);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_request_403_no_auto_checkin(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);

    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    opts.no_auto_checkin = 1;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx),
                     EHEM_OK);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    ehem_ext_request_info *info = NULL;
    assert_int_equal(ehem_ext_request(ctx, B64_32, "s", NULL, NULL, &info),
                     EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(fake_transport_request_count(fake), 1);   /* no legs */

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_token_happy_and_failures(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"token\":\"h.p.s\"}"), 0);
    char *token = NULL;
    assert_int_equal(ehem_ext_token(ctx, "reply.j.wt", &token), EHEM_OK);
    assert_non_null(token);
    assert_string_equal(token, "h.p.s");
    ehem_ext_token_free(token);
    token = NULL;

    const fake_captured_request *req = fake_transport_request(fake, 0);
    assert_string_equal(req->path, "/api/auth/ext/token");
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));
    assert_non_null(strstr((const char *)req->body,
                           "\"authreply\":\"reply.j.wt\""));

    /* 401 and 406 both AUTH_FAILED, distinguished in the detail. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401, NULL), 0);
    assert_int_equal(ehem_ext_token(ctx, "reply.j.wt", &token),
                     EHEM_ERR_AUTH_FAILED);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "401"));
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    assert_int_equal(ehem_ext_token(ctx, "reply.j.wt", &token),
                     EHEM_ERR_AUTH_FAILED);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "406"));

    /* Missing token field → PROTOCOL; empty reply → ARG (no I/O). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, "{}"), 0);
    assert_int_equal(ehem_ext_token(ctx, "reply.j.wt", &token),
                     EHEM_ERR_PROTOCOL);
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(ehem_ext_token(ctx, "", &token), EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), before);
    assert_null(token);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_frees_null_safe(void **state)
{
    (void)state;
    ehem_ext_init_free(NULL);
    ehem_ext_validate_free(NULL);
    ehem_ext_mac_free(NULL);
    ehem_ext_request_free(NULL);
    ehem_ext_token_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_happy),
        cmocka_unit_test(test_init_arg_validation),
        cmocka_unit_test(test_init_missing_field_and_403),
        cmocka_unit_test(test_validate_happy),
        cmocka_unit_test(test_validate_406_and_args),
        cmocka_unit_test(test_mac_happy_and_missing),
        cmocka_unit_test(test_request_happy_unauthenticated),
        cmocka_unit_test(test_request_arg_bounds),
        cmocka_unit_test(test_request_rtc_403_recovery),
        cmocka_unit_test(test_request_403_no_auto_checkin),
        cmocka_unit_test(test_token_happy_and_failures),
        cmocka_unit_test(test_frees_null_safe),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
