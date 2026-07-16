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
#include "proto_common.h"     /* internal: scoped request path (REQ-AUTH-003) */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "json.h"             /* internal: inspect a scoped response body */
#include "transport.h"        /* internal: ehem_http_method, TLS overrides */
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

/* A scope + path for the authenticated-request-path tests (no real keymgmt
 * binding exists until M3; the scoped call is driven straight through
 * ehem_proto_request_json). */
#define SCOPED_PATH  "/api/keymgmt/list"
#define SCOPED_SCOPE "keymgmt:list"

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

/* Cheap PBKDF2 count for tests that don't check derived bytes: a 600k-round
 * login KDF is ~0.5s, and this suite performs ~20 acquisitions. */
#define FAST_KDF_ITERS 1000

/* Per-test env setup: pin the auth clock and default to the cheap KDF count.
 * Every test calls this at its top, so no test depends on another's cleanup of
 * the global seams. The one byte-exact test overrides the KDF count back to the
 * production value. */
static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
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

/* Build a crafted bearer "hdr.<b64url({"exp":N,"k":tag})>.sig": the `exp` claim
 * is what the cache reads back; `tag` distinguishes otherwise-equal tokens so a
 * re-acquisition is observable. */
static void make_token(int64_t token_exp, const char *tag, char *out, size_t cap)
{
    char payload[96], seg[160];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                     (long long)token_exp, tag);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m,
                                   seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(out, cap, "hdr.%s.sig", seg);
}

/* Queue a {"token":"<make_token>"} response. */
static void push_token(ehem_transport *fake, int64_t token_exp, const char *tag)
{
    char tok[256], resp[768];
    make_token(token_exp, tag, tok, sizeof tok);
    snprintf(resp, sizeof resp, "{\"token\":\"%s\"}", tok);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* Assert request `i` carries Authorization: Bearer <the token made from these
 * args> — i.e. the binding sent exactly the token the cache acquired. */
static void assert_bearer(ehem_transport *fake, size_t i,
                          int64_t token_exp, const char *tag)
{
    char tok[256], expect[300];
    make_token(token_exp, tag, tok, sizeof tok);
    snprintf(expect, sizeof expect, "Bearer %s", tok);
    assert_string_equal(fake_transport_request_header(fake, i, "Authorization"),
                        expect);
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
    /* This is the byte-exact check against the python fixture, so it must use
     * the real 600 000-round KDF (the fixture was captured with it). */
    ehem_auth_test_set_kdf_iters(600000);

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

/* The token's exp claim is the requested lifetime (now + 3600), NOT capped by
 * the challenge's short response deadline (STEP-M2-045). */
static void test_login_exp_is_requested_lifetime(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* challenge.exp = now + 10 (a short SUBMIT deadline) must NOT shorten the
     * token; the eJWT exp should still be now + 3600. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\","
        "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":1700000010}"), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "life");

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
    assert_non_null(strstr(payload, "\"exp\":1700003600"));   /* now + 3600, uncapped */

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

/* -------------------------------------------------------------------------- */
/* REQ-AUTH-003: authenticated request path                                   */
/* -------------------------------------------------------------------------- */

/* A scoped request carries Authorization: Bearer <token>; the unauthenticated
 * login exchange that precedes it does not. */
static void test_scoped_request_sends_bearer(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "sc");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ok\":1}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, SCOPED_PATH,
                                             NULL, SCOPED_SCOPE,
                                             EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_OK);
    assert_non_null(root);

    /* login GET + login POST + the scoped GET. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));  /* challenge */
    assert_null(fake_transport_request_header(fake, 1, "Authorization"));  /* token POST */
    assert_string_equal(fake_transport_request(fake, 2)->path, SCOPED_PATH);
    assert_bearer(fake, 2, EJWT_FX_NOW + 100000, "sc");

    ehem_json_free(root);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 401 with a cached token → one re-acquire + one retry → still 401 →
 * EHEM_ERR_AUTH_FAILED. */
static void test_scoped_401_reacquire_then_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"revoked\"}"), 0);              /* first try  */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);   /* re-acq */
    push_token(fake, EJWT_FX_NOW + 100000, "t2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"still bad\"}"), 0);            /* retry      */

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, SCOPED_PATH,
                                             NULL, SCOPED_SCOPE,
                                             EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_ERR_AUTH_FAILED);
    assert_null(root);

    /* login(2) + scoped(401) + re-login(2) + scoped-retry(401) = 6, one retry. */
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_string_equal(fake_transport_request(fake, 2)->path, SCOPED_PATH);
    assert_bearer(fake, 2, EJWT_FX_NOW + 100000, "t1");
    assert_string_equal(fake_transport_request(fake, 5)->path, SCOPED_PATH);
    assert_true(fake_transport_request(fake, 5)->fresh_connection);
    assert_bearer(fake, 5, EJWT_FX_NOW + 100000, "t2");
    assert_int_equal(ehem_last_error(ctx)->http_status, 401);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 401 then a fresh token succeeds on the single retry. */
static void test_scoped_401_reacquire_then_success(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"revoked\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ok\":1}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, SCOPED_PATH,
                                             NULL, SCOPED_SCOPE,
                                             EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_OK);
    assert_non_null(root);
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_bearer(fake, 5, EJWT_FX_NOW + 100000, "t2");   /* retry used new token */

    ehem_json_free(root);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 403 → EHEM_ERR_SCOPE_DENIED with the device payload; no retry. */
static void test_scoped_403_scope_denied(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
                     "{\"error\":\"scope not granted\"}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, SCOPED_PATH,
                                             NULL, SCOPED_SCOPE,
                                             EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_ERR_SCOPE_DENIED);
    assert_null(root);
    /* No auth retry on 403 — just login(2) + the scoped GET. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "scope not granted"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Composition guard (REQ-AUTH-003 × REQ-NET-005): a scoped request that first
 * hits an expired cert (→ one check-in recovery) and then a 401 (→ one
 * re-acquire retry) runs each recovery exactly once and then succeeds. */
static void test_auth_and_checkin_compose_once_each(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* Initial token acquisition. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t1");
    /* Scoped send: expired cert → check-in (3 legs). */
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_OK), 0);
    /* Cert-recovery resend → 401 → auth re-acquire (2) → retry → 200. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"revoked\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    push_token(fake, EJWT_FX_NOW + 100000, "t2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ok\":1}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);

    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, SCOPED_PATH,
                                             NULL, SCOPED_SCOPE,
                                             EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_OK);
    assert_non_null(root);
    assert_true(ehem_cert_refreshed(ctx));

    /* login(0,1) scoped(2,expired) checkin(3,4,5) resend(6,401) re-login(7,8)
     * retry(9,200) — 10 total: exactly one check-in and one auth retry. */
    assert_int_equal((int)fake_transport_request_count(fake), 10);
    assert_string_equal(fake_transport_request(fake, 2)->path, SCOPED_PATH);
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 5)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 6)->path, SCOPED_PATH);
    assert_bearer(fake, 6, EJWT_FX_NOW + 100000, "t1");   /* recovery resend, old token */
    assert_string_equal(fake_transport_request(fake, 9)->path, SCOPED_PATH);
    assert_bearer(fake, 9, EJWT_FX_NOW + 100000, "t2");   /* auth retry, new token */

    ehem_json_free(root);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A NULL-scope request through the shared path sends no Authorization and does
 * not auth-retry on 401 (it maps straight through). */
static void test_unscoped_no_bearer_no_retry(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
                     "{\"error\":\"nope\"}"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    /* No login at all: a NULL-scope request never touches the auth layer. */
    ehem_json *root = NULL;
    assert_int_equal(ehem_proto_request_json(ctx, EHEM_HTTP_GET, "/api/system/status",
                                             NULL, NULL, EHEM_TLS_REQ_DEFAULT, &root),
                     EHEM_ERR_AUTH_FAILED);   /* 401 mapped, no retry */
    assert_null(root);
    assert_int_equal((int)fake_transport_request_count(fake), 1);
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_login_full_sequence),
        cmocka_unit_test(test_login_exp_is_requested_lifetime),
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
        /* REQ-AUTH-003: authenticated request path. */
        cmocka_unit_test(test_scoped_request_sends_bearer),
        cmocka_unit_test(test_scoped_401_reacquire_then_failed),
        cmocka_unit_test(test_scoped_401_reacquire_then_success),
        cmocka_unit_test(test_scoped_403_scope_denied),
        cmocka_unit_test(test_auth_and_checkin_compose_once_each),
        cmocka_unit_test(test_unscoped_no_bearer_no_retry),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
