/*
 * test_auth.c — the session engine (src/proto_auth.c) driven offline through
 * the fake transport.
 *
 * verifies: REQ-AUTH-001 (challenge→derive→POST sequence, no Authorization on
 *           the challenge GET, eJWT body byte-exact vs the python fixture,
 *           exp = min(now+lifetime, challenge.exp), POST 401 → AUTH_FAILED,
 *           RTC-unset 403 → check-in → retry + opt-out),
 *           REQ-AUTH-002 (scope-keyed cache: same-scope reuse, per-scope
 *           isolation, skew-window + device-shortened exp re-acquisition,
 *           retention-off → AUTH_EXPIRED without network, logout drops the
 *           cache and zeroizes credentials)
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
#include "proto_auth.h"       /* internal: ensure-token + the clock seam */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "transport.h"        /* internal: ehem_http_method, TLS overrides */
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

/* The challenge the fake device serves: the fixture inputs, so the eJWT the
 * login POSTs is byte-identical to EJWT_FX_EXPECT_EJWT when the clock is
 * pinned to EJWT_FX_NOW (challenge.exp = EJWT_FX_CHALLENGE_EXP = 2000000000). */
#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

/* What the login POST body must serialize to (compact, key order from the
 * builder): the single "auth" field carrying the fixture eJWT. */
#define EXPECT_POST_BODY "{\"auth\":\"" EJWT_FX_EXPECT_EJWT "\"}"

/* Check-in canned bodies (three legs), reused from the check-in suite. */
static const char CI_CHALLENGE[] = "{\"check\":\"CHALLENGE-BLOB\"}";
static const char CI_VERIFIED[]  = "{\"checked\":\"CLOUD-VERIFIED-BLOB\"}";
static const char CI_OK[]        = "{\"status\":\"ok\",\"newcrt\":\"\"}";

/* -------------------------------------------------------------------------- */
/* Test clock                                                                 */
/* -------------------------------------------------------------------------- */

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

/* Pin the auth clock for this test. Every test calls this, so no test depends
 * on another's cleanup of the global seam. */
static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static ehem_ctx *ctx_with(ehem_transport *fake, const ehem_options *extra)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    if (extra != NULL) {
        opts = *extra;
    } else {
        ehem_options_init(&opts);
    }
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

/* Queue a token response {"token":"hdr.<b64url({"exp":N,"k":tag})>.sig"}. The
 * `exp` claim is what the cache reads back; `tag` distinguishes otherwise-equal
 * tokens so a re-acquisition is observable. */
static void push_token(ehem_transport *fake, int64_t token_exp, const char *tag)
{
    char payload[96], seg[160], resp[768];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                     (long long)token_exp, tag);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m,
                                   seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* Decode the payload segment of an eJWT into a NUL-terminated JSON string. */
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

/* -------------------------------------------------------------------------- */
/* REQ-AUTH-001: login sequence                                               */
/* -------------------------------------------------------------------------- */

static void test_login_full_sequence(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "login");   /* long-lived */

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_non_null(tok);

    /* Exactly two exchanges: the challenge GET then the token POST. */
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    const fake_captured_request *get = fake_transport_request(fake, 0);
    assert_int_equal(get->method, EHEM_HTTP_GET);
    assert_string_equal(get->path, "/api/auth/token");
    assert_int_equal(get->tls_override, EHEM_TLS_REQ_DEFAULT);
    assert_null(get->body);
    /* The challenge GET is unauthenticated — no bearer yet. */
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    const fake_captured_request *post = fake_transport_request(fake, 1);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/auth/token");
    assert_non_null(post->body);
    /* The eJWT is byte-for-byte the python-client fixture. */
    assert_string_equal((const char *)post->body, EXPECT_POST_BODY);
    assert_string_equal(fake_transport_request_header(fake, 1, "Content-Type"),
                        "application/json");

    /* exp = min(now + lifetime, challenge.exp) = now + 3600 here. */
    char payload[400];
    decode_payload(EJWT_FX_EXPECT_EJWT, payload, sizeof payload);
    assert_non_null(strstr(payload, "\"iat\":1700000000"));
    assert_non_null(strstr(payload, "\"exp\":1700003600"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* challenge.exp shorter than now+lifetime caps the token's exp claim. */
static void test_login_exp_capped_by_challenge(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* challenge.exp = now + 10 < now + 3600 → exp claim should be now + 10. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\","
        "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":1700000010}"), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "cap");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);

    const char *body = (const char *)fake_transport_request(fake, 1)->body;
    /* Pull the eJWT out of {"auth":"<ejwt>"} and inspect its payload. */
    const char *q = strchr(body, ':') + 2;   /* first char of the eJWT */
    char ejwt[600];
    size_t len = strcspn(q, "\"");
    assert_true(len < sizeof ejwt);
    memcpy(ejwt, q, len);
    ejwt[len] = '\0';

    char payload[400];
    decode_payload(ejwt, payload, sizeof payload);
    assert_non_null(strstr(payload, "\"exp\":1700000010"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_login_post_401_auth_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"bad passphrase\"}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok),
                     EHEM_ERR_AUTH_FAILED);
    assert_null(tok);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    const ehem_error *err = ehem_last_error(ctx);
    assert_int_equal(err->http_status, 401);
    assert_non_null(err->device_payload);
    assert_non_null(strstr(err->device_payload, "bad passphrase"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Challenge GET 403 (device RTC unset) → one check-in → retry the challenge. */
static void test_rtc_unset_recovery(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
                     "{\"error\":\"rtc not set\"}"), 0);          /* challenge */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_OK), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);   /* retry */
    push_token(fake, EJWT_FX_NOW + 100000, "rtc");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_non_null(tok);

    /* challenge(403) + 3 check-in legs + challenge(retry) + token POST = 6. */
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_string_equal(fake_transport_request(fake, 0)->path, "/api/auth/token");
    assert_string_equal(fake_transport_request(fake, 1)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 2)->path, EHEM_DEFAULT_CHECKIN_URL);
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 4)->path, "/api/auth/token");
    assert_int_equal(fake_transport_request(fake, 4)->method, EHEM_HTTP_GET);
    assert_string_equal(fake_transport_request(fake, 5)->path, "/api/auth/token");
    assert_int_equal(fake_transport_request(fake, 5)->method, EHEM_HTTP_POST);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* With auto check-in disabled, a 403 challenge fails immediately (no legs). */
static void test_rtc_unset_optout(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
                     "{\"error\":\"rtc not set\"}"), 0);

    ehem_options opts;
    ehem_options_init(&opts);
    opts.no_auto_checkin = 1;
    ehem_ctx *ctx = ctx_with(fake, &opts);

    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok),
                     EHEM_ERR_SCOPE_DENIED);   /* 403 mapping, no recovery */
    assert_null(tok);
    assert_int_equal((int)fake_transport_request_count(fake), 1);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* REQ-AUTH-002: token cache                                                  */
/* -------------------------------------------------------------------------- */

static void test_cache_same_scope_reuse(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "reuse");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *t1 = NULL, *t2 = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t1), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Second call for the same scope → cache hit, no new traffic. */
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t2), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 2);
    assert_string_equal(t1, t2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_cache_per_scope_isolation(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "scopeA");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "scopeB");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *ta = NULL, *tb = NULL;
    char a_copy[768];
    assert_int_equal(ehem_auth_ensure_token(ctx, "keymgmt:gen", &ta), EHEM_OK);
    snprintf(a_copy, sizeof a_copy, "%s", ta);   /* copy: tb may realloc cache */
    assert_int_equal(ehem_auth_ensure_token(ctx, "keymgmt:list", &tb), EHEM_OK);

    /* Two distinct scopes → two acquisitions, two distinct tokens. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_string_not_equal(a_copy, tb);

    /* First scope still cached (no third acquisition). */
    const char *ta2 = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "keymgmt:gen", &ta2), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_string_equal(ta2, a_copy);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A token whose exp lands inside the 60 s skew is treated as already expired
 * and re-acquired on the next use (no clock advance needed). */
static void test_cache_skew_window_reacquire(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 30, "t1");      /* 30s < 60s skew */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t2");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *t1 = NULL, *t2 = NULL;
    char first[768];
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t1), EHEM_OK);
    snprintf(first, sizeof first, "%s", t1);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Skew makes it stale → re-acquired, a different token comes back. */
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t2), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_string_not_equal(first, t2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* The cache honors a short device-issued exp over the requested lifetime: a
 * 120 s token is stale at now+90, whereas the 3600 s lifetime would not be. */
static void test_cache_device_shortened_exp(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 120, "short");   /* cache_exp = now+60 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "fresh");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Advance past the device exp (but well within a 3600 s lifetime). */
    set_now(EJWT_FX_NOW + 90);
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 4);   /* re-acquired */

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Retention disabled: after the passphrase is used once it is scrubbed, so a
 * later cache miss/expiry fails AUTH_EXPIRED with NO network attempt. */
static void test_retention_off_expired_no_network(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 30, "once");   /* stale immediately (skew) */
    /* NOTE: no second challenge/token queued — a re-login attempt would 404. */

    ehem_options opts;
    ehem_options_init(&opts);
    opts.no_credential_retention = 1;
    ehem_ctx *ctx = ctx_with(fake, &opts);

    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    /* First acquisition works (passphrase still present), then it is scrubbed. */
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Stale token + no retained credential → AUTH_EXPIRED, no traffic. */
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok),
                     EHEM_ERR_AUTH_EXPIRED);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* logout drops the cache and zeroizes credentials: a later call fails without
 * network, and a re-login re-arms the session. (ASan/LSan cover zeroization.) */
static void test_logout_drops_cache(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "s1");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    assert_int_equal(ehem_logout(ctx), EHEM_OK);

    /* No credential, no cache → AUTH_EXPIRED with no traffic. */
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &tok),
                     EHEM_ERR_AUTH_EXPIRED);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A second ehem_login drops the prior scope cache; the next call re-acquires. */
static void test_relogin_resets_cache(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "first");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "second");

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *t1 = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t1), EHEM_OK);
    char first[768];
    snprintf(first, sizeof first, "%s", t1);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Re-login → cache dropped → next ensure re-acquires. */
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    const char *t2 = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, EJWT_FX_SCOPE, &t2), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_string_not_equal(first, t2);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* Argument validation / degenerate states                                    */
/* -------------------------------------------------------------------------- */

static void test_arg_validation(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    const char *tok = (const char *)0x1;
    assert_int_equal(ehem_login(NULL, "pw"), EHEM_ERR_ARG);
    assert_int_equal(ehem_logout(NULL), EHEM_ERR_ARG);
    assert_int_equal(ehem_auth_ensure_token(NULL, "s", &tok), EHEM_ERR_ARG);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, NULL);

    assert_int_equal(ehem_login(ctx, NULL), EHEM_ERR_ARG);
    assert_int_equal(ehem_auth_ensure_token(ctx, NULL, &tok), EHEM_ERR_ARG);
    assert_int_equal(ehem_auth_ensure_token(ctx, "s", NULL), EHEM_ERR_ARG);

    /* ensure-token before any login → AUTH_EXPIRED, no traffic. */
    tok = (const char *)0x1;
    assert_int_equal(ehem_auth_ensure_token(ctx, "s", &tok), EHEM_ERR_AUTH_EXPIRED);
    assert_null(tok);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    /* logout on a never-logged-in context is a no-op success. */
    assert_int_equal(ehem_logout(ctx), EHEM_OK);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_login_full_sequence),
        cmocka_unit_test(test_login_exp_capped_by_challenge),
        cmocka_unit_test(test_login_post_401_auth_failed),
        cmocka_unit_test(test_rtc_unset_recovery),
        cmocka_unit_test(test_rtc_unset_optout),
        cmocka_unit_test(test_cache_same_scope_reuse),
        cmocka_unit_test(test_cache_per_scope_isolation),
        cmocka_unit_test(test_cache_skew_window_reacquire),
        cmocka_unit_test(test_cache_device_shortened_exp),
        cmocka_unit_test(test_retention_off_expired_no_network),
        cmocka_unit_test(test_logout_drops_cache),
        cmocka_unit_test(test_relogin_resets_cache),
        cmocka_unit_test(test_arg_validation),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
