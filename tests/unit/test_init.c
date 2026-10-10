/*
 * test_init.c — the device-initialisation binding (src/proto_auth.c
 * ehem_device_init) driven offline through the fake transport.
 *
 * verifies: REQ-AUTH-011 (GET challenge → PBKDF2 user key + master key →
 *           MASTER-signed init JWT → POST sequence; the posted JWT carries the
 *           full eJWT header the firmware requires (alg + ecdh — what the
 *           Manager really sends), the claims jti / aud / exp = the
 *           challenge's / iat / iss = master public key / cfg with all 13
 *           mandatory fields and the Manager defaults, and an HMAC-SHA256 tag
 *           the test recomputes from ECDH(master, spk); the returned
 *           system:config bearer is cached and the passphrase session is in
 *           place; 403/406/409/400/401 mapped with an explaining detail;
 *           required-field and abi_size validation with zero traffic;
 *           optional reply fields and a NULL out)
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
#include "proto_auth.h"       /* internal: ensure-token + clock/KDF seams */
#include "ejwt.h"             /* internal: base64 codecs */
#include "json.h"             /* internal: inspect the posted body */
#include "crypto_shim.h"      /* internal: recompute keys and the tag */
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define FAST_KDF_ITERS 1000

/* The init challenge the fake device serves (same eid/spk/jti as the login
 * fixture; exp is the challenge deadline the init JWT must echo). */
#define INIT_CHALLENGE_JSON \
    "{\"exp\":2000000000,\"spk\":\"" EJWT_FX_SPK "\",\"jti\":\"" EJWT_FX_JTI "\"," \
    "\"genuine\":\"GENUINE-CHALLENGE\",\"eid\":\"" EJWT_FX_EID "\"}"

static const uint8_t MASTER[EHEM_MASTER_SECRET_SIZE] = {
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

/* A wiped device has no HTTPS — the context is created with its http:// URL. */
static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("http://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

/* A bearer with a readable far-future exp, as the device returns one. */
static void make_token(char *out, size_t cap, const char *tag)
{
    char payload[96], seg[160];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                     (long long)(EJWT_FX_NOW + 100000), tag);
    size_t n = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(n, (size_t)-1);
    snprintf(out, cap, "hdr.%s.sig", seg);
}

static void fill_params(ehem_init_params *p)
{
    ehem_init_params_init(p);
    p->passphrase        = EJWT_FX_PASSPHRASE;
    p->master_secret     = MASTER;
    p->user              = "alice";
    p->email             = "alice@example.com";
    p->hostname          = "my.ence.do";
    p->ip                = "192.168.7.1/24";
    p->storage_mode      = 83;
    p->storage_disk0size = 8388608;
    p->gen_csr           = 1;
}

/* Split "h.p.s" into NUL-terminated copies (the payload is the long one). */
static void split_jwt(const char *jwt, char *h, size_t hcap,
                      char *p, size_t pcap, char *s, size_t scap)
{
    const char *d1 = strchr(jwt, '.');
    const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    assert_non_null(d1);
    assert_non_null(d2);
    assert_true((size_t)(d1 - jwt) < hcap);
    assert_true((size_t)(d2 - d1 - 1) < pcap);
    assert_true(strlen(d2 + 1) < scap);
    memcpy(h, jwt, (size_t)(d1 - jwt));       h[d1 - jwt] = '\0';
    memcpy(p, d1 + 1, (size_t)(d2 - d1 - 1)); p[d2 - d1 - 1] = '\0';
    strcpy(s, d2 + 1);
}

/* The full happy path: the exact JWT the Manager would have built. */
static void test_init_full(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    char token[256];
    make_token(token, sizeof token, "init");
    char reply[512];
    snprintf(reply, sizeof reply,
             "{\"reboot_required\":true,\"instanceid\":\"uuid-init-1\","
             "\"token\":\"%s\",\"csr\":\"-----BEGIN CERTIFICATE REQUEST-----\\nMIIB\\n"
             "-----END CERTIFICATE REQUEST-----\",\"genuine\":\"GEN-2\"}", token);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply), 0);

    ehem_ctx *ctx = ctx_with(fake);
    ehem_init_params p;
    fill_params(&p);
    ehem_init_info *info = NULL;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_OK);
    assert_non_null(info);
    assert_true(info->reboot_required);
    assert_string_equal(info->instanceid, "uuid-init-1");
    assert_non_null(info->csr);
    assert_non_null(strstr(info->csr, "BEGIN CERTIFICATE REQUEST"));
    assert_string_equal(info->genuine, "GEN-2");
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    /* Leg 1: unauthenticated GET. */
    const fake_captured_request *get = fake_transport_request(fake, 0);
    assert_int_equal(get->method, EHEM_HTTP_GET);
    assert_string_equal(get->path, "/api/auth/init");
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    /* Leg 2: POST {"init": "<jwt>"}, no bearer. */
    const fake_captured_request *post = fake_transport_request(fake, 1);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/auth/init");
    assert_null(fake_transport_request_header(fake, 1, "Authorization"));
    assert_string_equal(fake_transport_request_header(fake, 1, "Content-Type"),
                        "application/json");
    ehem_json *body = ehem_json_parse((const char *)post->body, post->body_len);
    assert_non_null(body);
    const char *jwt = NULL;
    assert_true(ehem_json_get_string(body, "init", &jwt));

    static char h[256], pl[2048], sig[128];
    split_jwt(jwt, h, sizeof h, pl, sizeof pl, sig, sizeof sig);

    /* Header: byte-exact what the Manager sends. The firmware refuses a header
     * without "alg":"HS256" with 401 before checking the signature (the bare
     * {"ecdh":"x25519"} this test once pinned failed the first live init), and
     * keys the HMAC with ECDH only when "ecdh":"x25519" is present. */
    uint8_t hdr[256];
    size_t hn = ehem_b64url_decode(h, strlen(h), hdr, sizeof hdr);
    assert_int_equal(hn, strlen(EHEM_EJWT_HEADER));
    assert_memory_equal(hdr, EHEM_EJWT_HEADER, hn);
    hdr[hn] = '\0';
    assert_non_null(strstr((const char *)hdr, "\"alg\":\"HS256\""));
    assert_non_null(strstr((const char *)hdr, "\"ecdh\":\"x25519\""));

    /* The personas, recomputed exactly as the binding must have done. */
    uint8_t seed[32], user_priv[32], user_pub[32], master_priv[32], master_pub[32];
    assert_int_equal(ehem_kdf_pbkdf2_sha256((const uint8_t *)EJWT_FX_PASSPHRASE,
                                            strlen(EJWT_FX_PASSPHRASE),
                                            (const uint8_t *)EJWT_FX_EID, strlen(EJWT_FX_EID),
                                            FAST_KDF_ITERS, seed, sizeof seed), EHEM_OK);
    assert_int_equal(ehem_x25519_keypair_from_seed(seed, user_priv, user_pub), EHEM_OK);
    assert_int_equal(ehem_x25519_keypair_from_seed(MASTER, master_priv, master_pub), EHEM_OK);
    char user_b64[64], master_b64[64];
    assert_int_not_equal(ehem_b64_std_encode(user_pub, 32, user_b64, sizeof user_b64), (size_t)-1);
    assert_int_not_equal(ehem_b64_std_encode(master_pub, 32, master_b64, sizeof master_b64), (size_t)-1);

    /* Claims. */
    uint8_t plbuf[2048];
    size_t pn = ehem_b64url_decode(pl, strlen(pl), plbuf, sizeof plbuf);
    assert_int_not_equal(pn, (size_t)-1);
    ehem_json *claims = ehem_json_parse((const char *)plbuf, pn);
    assert_non_null(claims);
    const char *s = NULL;
    int64_t n64 = 0;
    bool b = false;
    assert_true(ehem_json_get_string(claims, "jti", &s)); assert_string_equal(s, EJWT_FX_JTI);
    assert_true(ehem_json_get_string(claims, "aud", &s)); assert_string_equal(s, EJWT_FX_SPK);
    assert_true(ehem_json_get_int64(claims, "exp", &n64)); assert_true(n64 == 2000000000);
    assert_true(ehem_json_get_int64(claims, "iat", &n64)); assert_true(n64 == EJWT_FX_NOW);
    assert_true(ehem_json_get_string(claims, "iss", &s)); assert_string_equal(s, master_b64);
    assert_false(ehem_json_has(claims, "scope"));         /* not a login eJWT */

    const ehem_json *cfg = ehem_json_get(claims, "cfg");
    assert_non_null(cfg);
    assert_true(ehem_json_get_string(cfg, "masterkey", &s)); assert_string_equal(s, master_b64);
    assert_true(ehem_json_get_string(cfg, "userkey", &s));   assert_string_equal(s, user_b64);
    assert_true(ehem_json_get_string(cfg, "user", &s));      assert_string_equal(s, "alice");
    assert_true(ehem_json_get_string(cfg, "email", &s));     assert_string_equal(s, "alice@example.com");
    assert_true(ehem_json_get_string(cfg, "hostname", &s));  assert_string_equal(s, "my.ence.do");
    assert_true(ehem_json_get_string(cfg, "ip", &s));        assert_string_equal(s, "192.168.7.1/24");
    assert_true(ehem_json_get_int64(cfg, "storage_mode", &n64));      assert_true(n64 == 83);
    assert_true(ehem_json_get_int64(cfg, "storage_disk0size", &n64)); assert_true(n64 == 8388608);
    assert_true(ehem_json_get_bool(cfg, "dnsd", &b));            assert_false(b);
    assert_true(ehem_json_get_bool(cfg, "trusted_ts", &b));      assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "trusted_backend", &b)); assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "allow_keysearch", &b)); assert_true(b);
    assert_true(ehem_json_get_string(cfg, "origin", &s));        assert_string_equal(s, "*");
    assert_true(ehem_json_get_int64(cfg, "ctx", &n64));          assert_true(n64 == 0);
    assert_true(ehem_json_get_bool(cfg, "gen_csr", &b));         assert_true(b);

    /* Tag: HMAC-SHA256 over "<header>.<payload>" keyed with ECDH(master, spk). */
    uint8_t spk[32], shared[32], mac[32];
    assert_int_equal(ehem_b64_std_decode(EJWT_FX_SPK, strlen(EJWT_FX_SPK), spk, sizeof spk), 32);
    assert_int_equal(ehem_x25519_shared(master_priv, spk, shared), EHEM_OK);
    size_t sign_len = strlen(h) + 1 + strlen(pl);
    assert_int_equal(ehem_hmac_sha256(shared, 32, (const uint8_t *)jwt, sign_len, mac), EHEM_OK);
    char expect_sig[64];
    assert_int_not_equal(ehem_b64url_encode(mac, 32, expect_sig, sizeof expect_sig), (size_t)-1);
    assert_string_equal(sig, expect_sig);

    ehem_json_free(claims);
    ehem_json_free(body);

    /* The returned bearer is cached for system:config: no login traffic. */
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:config", &tok), EHEM_OK);
    assert_string_equal(tok, token);
    assert_int_equal((int)fake_transport_request_count(fake), 2);

    ehem_init_info_free(info);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Manager defaults for the optional cfg fields; a minimal reply; out may be
 * NULL; the inverted-flag inputs flip the booleans. */
static void test_init_defaults_and_optional(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    char token[256];
    make_token(token, sizeof token, "init2");
    char reply[384];
    snprintf(reply, sizeof reply, "{\"instanceid\":\"uuid-init-2\",\"token\":\"%s\"}", token);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply), 0);

    ehem_ctx *ctx = ctx_with(fake);
    ehem_init_params p;
    fill_params(&p);
    p.gen_csr = 0;
    p.origin  = "https://app.example";
    p.dnsd    = 1;
    p.no_trusted_ts = 1;
    p.no_allow_keysearch = 1;
    p.ctx_id  = 7;
    assert_int_equal(ehem_device_init(ctx, &p, NULL), EHEM_OK);    /* NULL out */

    const fake_captured_request *post = fake_transport_request(fake, 1);
    ehem_json *body = ehem_json_parse((const char *)post->body, post->body_len);
    const char *jwt = NULL;
    assert_true(ehem_json_get_string(body, "init", &jwt));
    static char h[256], pl[2048], sig[128];
    split_jwt(jwt, h, sizeof h, pl, sizeof pl, sig, sizeof sig);
    uint8_t plbuf[2048];
    size_t pn = ehem_b64url_decode(pl, strlen(pl), plbuf, sizeof plbuf);
    ehem_json *claims = ehem_json_parse((const char *)plbuf, pn);
    const ehem_json *cfg = ehem_json_get(claims, "cfg");
    const char *s = NULL; bool b = false; int64_t n64 = 0;
    assert_true(ehem_json_get_string(cfg, "origin", &s));        assert_string_equal(s, "https://app.example");
    assert_true(ehem_json_get_bool(cfg, "dnsd", &b));            assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "trusted_ts", &b));      assert_false(b);
    assert_true(ehem_json_get_bool(cfg, "trusted_backend", &b)); assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "allow_keysearch", &b)); assert_false(b);
    assert_true(ehem_json_get_bool(cfg, "gen_csr", &b));         assert_false(b);
    assert_true(ehem_json_get_int64(cfg, "ctx", &n64));          assert_true(n64 == 7);
    ehem_json_free(claims);
    ehem_json_free(body);

    /* Second init on the same context (a re-init after a wipe): the minimal
     * reply yields the defaults. */
    ehem_init_info *info = NULL;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_OK);
    assert_non_null(info);
    assert_false(info->reboot_required);
    assert_string_equal(info->instanceid, "uuid-init-2");
    assert_null(info->csr);
    assert_null(info->genuine);
    ehem_init_info_free(info);
    ehem_init_info_free(NULL);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device preconditions and rejections — mapped with an explaining detail. */
static void test_init_device_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_init_params p;
    fill_params(&p);
    ehem_init_info *info = NULL;

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403, NULL), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "RTC"));
    assert_null(info);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "already initialised"));

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409, NULL), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 409);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "self-test"));

    /* Commit-leg rejections. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 400, NULL), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 400);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "cfg rejected"));

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401, NULL), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_AUTH_FAILED);
    assert_int_equal(ehem_last_error(ctx)->http_status, 401);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "JWT rejected"));

    /* A malformed challenge / reply is PROTOCOL. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"spk\":\"" EJWT_FX_SPK "\",\"jti\":\"x\"}"), 0);         /* no eid / exp */
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_PROTOCOL);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, INIT_CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, "{\"instanceid\":\"u\"}"), 0);
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_PROTOCOL);
    assert_int_equal((int)fake_transport_request_count(fake), 10);

    /* None of the failures left a session behind. */
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:config", &tok), EHEM_ERR_AUTH_EXPIRED);
    assert_int_equal((int)fake_transport_request_count(fake), 10);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Argument validation: zero traffic, EHEM_ERR_ARG with a reason. */
static void test_init_arg_validation(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_init_params p;
    ehem_init_info *info = (ehem_init_info *)0x1;

    assert_int_equal(ehem_device_init(NULL, &p, &info), EHEM_ERR_ARG);
    assert_int_equal(ehem_device_init(ctx, NULL, &info), EHEM_ERR_ARG);
    assert_null(info);

    memset(&p, 0, sizeof p);                              /* not stamped */
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "ehem_init_params_init"));

    fill_params(&p);
    p.hostname = NULL;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "required"));

    fill_params(&p);
    p.master_secret = NULL;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_ARG);

    fill_params(&p);
    p.storage_mode = 0;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_ARG);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "positive"));

    fill_params(&p);
    p.storage_disk0size = -1;
    assert_int_equal(ehem_device_init(ctx, &p, &info), EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_full),
        cmocka_unit_test(test_init_defaults_and_optional),
        cmocka_unit_test(test_init_device_errors),
        cmocka_unit_test(test_init_arg_validation),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
