/*
 * test_config.c — the system config + reboot bindings (REQ-SYS-004, REQ-SYS-005)
 * driven offline through the fake transport.
 *
 * verifies: REQ-SYS-004 (GET /api/system/config typed + tolerant parse, scope
 *           system:config, cert-install POST body {"tls":{"crt":…}} +
 *           updated/reboot_required, 400/409 mapping, _free NULL-safe),
 *           REQ-SYS-005 (reboot: authenticated GET, accepts the empty 200 the
 *           device sends, drops the token cache on success)
 *
 * Each binding is authenticated, so every call is preceded by a login exchange
 * (challenge GET + token POST) queued via push_login().
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
#include "ehem/system.h"
#include "proto_auth.h"       /* internal: ensure-token, clock/KDF seams */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

/* The login challenge the fake device serves (fixture inputs). */
#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

/* Cheap KDF for tests that don't check derived bytes. */
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

/* Queue a login exchange: a challenge GET response + a long-lived token POST
 * response, so the next authenticated binding acquires a bearer. */
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

/* Create a context and log in (lazy — no network yet). */
static ehem_ctx *logged_in_ctx(ehem_transport *fake)
{
    ehem_ctx *ctx = ctx_with(fake);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    return ctx;
}

/* -------------------------------------------------------------------------- */
/* config GET (REQ-SYS-004)                                                   */
/* -------------------------------------------------------------------------- */

static const char CONFIG_FULL[] =
    "{\"iat\":1769356977,\"uts\":1769356978,\"devid\":\"3dfd39eb56787905\","
    "\"eid\":\"EIDBASE64\",\"eid_sign\":\"SIGN\",\"user\":\"usb C\","
    "\"email\":\"\",\"hostname\":\"my.ence.do\",\"dnsd\":false,"
    "\"trusted_ts\":true,\"trusted_backend\":true,\"allow_keysearch\":true,"
    "\"origin\":\"*\",\"ctx\":0,\"ip\":\"192.168.7.1/24\","
    "\"http_option_hsts\":false,\"http_option_dosprot_mode\":0,"
    "\"genuine_id\":\"0123fd3c60540abbee\",\"instanceid\":\"uuid-1\","
    "\"storage_mode\":81,\"storage_disk0size\":8388607,"
    "\"storage_capacity\":242753531,"
    "\"spk\":\"SESSIONKEY\",\"nonce\":\"SESSIONNONCE\"}";

static void test_config_get_full(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "cfg");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CONFIG_FULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    assert_non_null(cfg);

    /* Required core. */
    assert_string_equal(cfg->devid, "3dfd39eb56787905");
    assert_string_equal(cfg->hostname, "my.ence.do");
    assert_string_equal(cfg->user, "usb C");

    /* Optional strings (email is present-but-empty). */
    assert_string_equal(cfg->email, "");
    assert_string_equal(cfg->eid, "EIDBASE64");
    assert_string_equal(cfg->instanceid, "uuid-1");
    assert_string_equal(cfg->origin, "*");
    assert_string_equal(cfg->ip, "192.168.7.1/24");
    assert_string_equal(cfg->genuine_id, "0123fd3c60540abbee");

    /* Optional numbers. */
    assert_true(cfg->has_iat);               assert_int_equal(cfg->iat, 1769356977);
    assert_true(cfg->has_uts);               assert_int_equal(cfg->uts, 1769356978);
    assert_true(cfg->has_ctx);               assert_int_equal(cfg->ctx, 0);
    assert_true(cfg->has_storage_mode);      assert_int_equal(cfg->storage_mode, 81);
    assert_true(cfg->has_storage_disk0size); assert_int_equal(cfg->storage_disk0size, 8388607);
    assert_true(cfg->has_storage_capacity);  assert_int_equal(cfg->storage_capacity, 242753531);

    /* Optional booleans. */
    assert_true(cfg->has_dnsd);            assert_false(cfg->dnsd);
    assert_true(cfg->has_trusted_ts);      assert_true(cfg->trusted_ts);
    assert_true(cfg->has_trusted_backend); assert_true(cfg->trusted_backend);
    assert_true(cfg->has_allow_keysearch); assert_true(cfg->allow_keysearch);
    assert_true(cfg->has_http_hsts);       assert_false(cfg->http_hsts);

    /* The scoped GET carries a bearer; the challenge GET did not. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_string_equal(fake_transport_request(fake, 2)->path, "/api/system/config");
    assert_int_equal(fake_transport_request(fake, 2)->method, EHEM_HTTP_GET);
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    ehem_system_config_free(cfg);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Minimal response: only the required core; optionals absent. */
static void test_config_get_minimal(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "min");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"devid\":\"D\",\"hostname\":\"H\",\"user\":\"U\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    assert_non_null(cfg);

    assert_string_equal(cfg->devid, "D");
    assert_string_equal(cfg->hostname, "H");
    assert_string_equal(cfg->user, "U");
    assert_null(cfg->email);
    assert_null(cfg->ip);
    assert_false(cfg->has_iat);
    assert_false(cfg->has_ctx);
    assert_false(cfg->has_dnsd);
    assert_false(cfg->has_http_hsts);

    ehem_system_config_free(cfg);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A missing required field is EHEM_ERR_PROTOCOL with detail. */
static void test_config_missing_required(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "bad");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"devid\":\"D\",\"hostname\":\"H\"}"), 0);   /* no "user" */

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_ERR_PROTOCOL);
    assert_null(cfg);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "user"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* cert install (REQ-SYS-004)                                                 */
/* -------------------------------------------------------------------------- */

static void test_install_cert_body_and_flags(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ins");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"updated\":true,\"reboot_required\":true}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_cert_install_info *res = NULL;
    assert_int_equal(
        ehem_system_config_install_cert(ctx, "TESTCERTB64", &res), EHEM_OK);
    assert_non_null(res);
    assert_true(res->updated);
    assert_true(res->reboot_required);

    /* The POST goes to config with the exact cert-only body and a bearer. */
    const fake_captured_request *post = fake_transport_request(fake, 2);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/system/config");
    assert_non_null(post->body);
    assert_string_equal((const char *)post->body, "{\"tls\":{\"crt\":\"TESTCERTB64\"}}");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_string_equal(fake_transport_request_header(fake, 2, "Content-Type"),
                        "application/json");

    ehem_cert_install_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* reboot_required absent → false; out may be NULL. */
static void test_install_cert_no_reboot_and_null_out(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ins2");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"updated\":true}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    /* NULL out is allowed (caller doesn't need the flags). */
    assert_int_equal(
        ehem_system_config_install_cert(ctx, "C", NULL), EHEM_OK);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_install_cert_400_validator(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "e400");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 400,
        "{\"error\":\"bad certificate\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_cert_install_info *res = NULL;
    assert_int_equal(
        ehem_system_config_install_cert(ctx, "C", &res), EHEM_ERR_DEVICE);
    assert_null(res);
    assert_int_equal(ehem_last_error(ctx)->http_status, 400);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "bad certificate"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_install_cert_409_in_progress(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "e409");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409,
        "{\"error\":\"install in progress\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(
        ehem_system_config_install_cert(ctx, "C", NULL), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 409);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* reboot (REQ-SYS-005)                                                       */
/* -------------------------------------------------------------------------- */

/* The device answers 200 with an empty body — success — and the binding drops
 * the token cache, so a subsequent authenticated call re-acquires. */
static void test_reboot_empty_200_drops_cache(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "rb1");                                    /* req 0,1 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);  /* req 2 */

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_system_reboot(ctx), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    const fake_captured_request *rb = fake_transport_request(fake, 2);
    assert_int_equal(rb->method, EHEM_HTTP_GET);
    assert_string_equal(rb->path, "/api/system/reboot");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));

    /* Cache was dropped: the next scoped token acquisition re-logs-in. */
    push_login(fake, "rb2");                                    /* req 3,4 */
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:config", &tok), EHEM_OK);
    assert_non_null(tok);
    assert_int_equal((int)fake_transport_request_count(fake), 5);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A reboot rejected for scope maps normally and does NOT drop the cache. */
static void test_reboot_403(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "rb403");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
        "{\"error\":\"forbidden\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_system_reboot(ctx), EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);

    /* Token still cached (reboot failed) → a scoped call reuses it, no re-login. */
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:config", &tok), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);  /* no extra login */

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_free_null_safe(void **state)
{
    (void)state;
    ehem_system_config_free(NULL);
    ehem_cert_install_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_get_full),
        cmocka_unit_test(test_config_get_minimal),
        cmocka_unit_test(test_config_missing_required),
        cmocka_unit_test(test_install_cert_body_and_flags),
        cmocka_unit_test(test_install_cert_no_reboot_and_null_out),
        cmocka_unit_test(test_install_cert_400_validator),
        cmocka_unit_test(test_install_cert_409_in_progress),
        cmocka_unit_test(test_reboot_empty_200_drops_cache),
        cmocka_unit_test(test_reboot_403),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
