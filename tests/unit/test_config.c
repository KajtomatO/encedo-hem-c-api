/*
 * test_config.c — the system config + reboot bindings (REQ-SYS-004, REQ-SYS-005)
 * driven offline through the fake transport.
 *
 * verifies: REQ-SYS-004 (GET /api/system/config typed + tolerant parse, scope
 *           system:config, cert-install POST body {"tls":{"crt":…}} +
 *           updated/reboot_required, 400/409 mapping, _free NULL-safe),
 *           REQ-SYS-005 (reboot: authenticated GET, accepts the empty 200 the
 *           device sends, drops the token cache on success),
 *           REQ-SYS-007 (selftest: full/minimal shapes, kat_busy/se_state/
 *           repo_stats defaults, missing fls_state → PROTOCOL),
 *           REQ-SYS-011 (attestation: crt vs csr+key shapes, missing genuine
 *           → PROTOCOL, 500 atecc_N payload preserved),
 *           REQ-SYS-008 (shutdown: empty-200 success + full cache drop, 409
 *           kept as DEVICE with cache intact)
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

/* -------------------------------------------------------------------------- */
/* selftest (REQ-SYS-007)                                                     */
/* -------------------------------------------------------------------------- */

static void test_selftest_full_shape(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "st");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"last_selftest_ts\":1705312200,\"last_fls_state\":0,"
        "\"last_entropytest_ts\":1705311000,\"last_kat_ts\":1705300000,"
        "\"kat_busy\":true,\"fls_state\":0,\"selftest_ts\":1705312300,"
        "\"repo_stats\":{\"total\":128,\"deleted\":5,\"invalid\":0,"
        "\"fragmented\":2,\"freeslots\":867},\"se_state\":0,"
        "\"junk\":\"ignored\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_selftest_info *info = NULL;
    assert_int_equal(ehem_system_selftest(ctx, &info), EHEM_OK);
    assert_non_null(info);
    assert_int_equal((int)info->fls_state, 0);
    assert_int_equal((long)info->selftest_ts, 1705312300L);
    assert_int_equal((long)info->last_selftest_ts, 1705312200L);
    assert_int_equal((long)info->last_entropytest_ts, 1705311000L);
    assert_int_equal((long)info->last_kat_ts, 1705300000L);
    assert_true(info->kat_busy);
    assert_int_equal((int)info->se_state, 0);
    assert_int_equal((int)info->repo_total, 128);
    assert_int_equal((int)info->repo_deleted, 5);
    assert_int_equal((int)info->repo_invalid, 0);
    assert_int_equal((int)info->repo_fragmented, 2);
    assert_int_equal((int)info->repo_freeslots, 867);

    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_int_equal(r->method, EHEM_HTTP_GET);
    assert_string_equal(r->path, "/api/system/selftest");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));

    ehem_selftest_free(info);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Minimal body (EPA, no KAT running): kat_busy → false, se_state / repo_* →
 * -1; missing fls_state → PROTOCOL. */
static void test_selftest_minimal_and_missing(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "stm");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"fls_state\":3,\"selftest_ts\":42}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"selftest_ts\":42}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_selftest_info *info = NULL;
    assert_int_equal(ehem_system_selftest(ctx, &info), EHEM_OK);
    assert_int_equal((int)info->fls_state, 3);   /* a FAIL verdict passes through */
    assert_false(info->kat_busy);
    assert_int_equal((int)info->se_state, -1);
    assert_int_equal((int)info->repo_total, -1);
    assert_int_equal((int)info->repo_freeslots, -1);
    ehem_selftest_free(info);

    info = NULL;
    assert_int_equal(ehem_system_selftest(ctx, &info), EHEM_ERR_PROTOCOL);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* attestation (REQ-SYS-011)                                                  */
/* -------------------------------------------------------------------------- */

static void test_attestation_both_shapes(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "at");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"crt\":\"REVSQ0VSVA==\",\"genuine\":\"tok1\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"csr\":\"-----BEGIN CERTIFICATE REQUEST-----\","
        "\"key\":\"REVSS0VZ\",\"genuine\":\"tok2\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_attestation_info *a = NULL;

    /* Provisioned shape. */
    assert_int_equal(ehem_system_attestation(ctx, &a), EHEM_OK);
    assert_string_equal(a->crt_b64, "REVSQ0VSVA==");
    assert_string_equal(a->genuine, "tok1");
    assert_null(a->csr_pem);
    assert_null(a->key_b64);
    ehem_attestation_free(a);
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/system/config/attestation");

    /* Fresh-chip shape. */
    a = NULL;
    assert_int_equal(ehem_system_attestation(ctx, &a), EHEM_OK);
    assert_null(a->crt_b64);
    assert_non_null(a->csr_pem);
    assert_string_equal(a->key_b64, "REVSS0VZ");
    assert_string_equal(a->genuine, "tok2");
    ehem_attestation_free(a);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Missing genuine → PROTOCOL; 500 "atecc_1" → DEVICE with the body kept. */
static void test_attestation_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ate");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"crt\":\"REVS\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 500,
        "atecc_1"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_attestation_info *a = NULL;
    assert_int_equal(ehem_system_attestation(ctx, &a), EHEM_ERR_PROTOCOL);
    assert_null(a);
    assert_int_equal(ehem_system_attestation(ctx, &a), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 500);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "atecc_1"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* shutdown (REQ-SYS-008)                                                     */
/* -------------------------------------------------------------------------- */

/* Mirrors the reboot convention: empty 200 == success + full cache drop. */
static void test_shutdown_empty_200_drops_cache(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "sd1");                                    /* req 0,1 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_system_shutdown(ctx), EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    const fake_captured_request *sd = fake_transport_request(fake, 2);
    assert_int_equal(sd->method, EHEM_HTTP_GET);
    assert_string_equal(sd->path, "/api/system/shutdown");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));

    /* Cache dropped: the next scoped acquisition re-logs-in. */
    push_login(fake, "sd2");
    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:shutdown", &tok),
                     EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 5);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 409 (install in progress) → DEVICE; cache kept. */
static void test_shutdown_409(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "sd9");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409,
        "{\"error\":\"busy\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_system_shutdown(ctx), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 409);

    const char *tok = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, "system:shutdown", &tok),
                     EHEM_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);  /* cached */

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_free_null_safe(void **state)
{
    (void)state;
    ehem_system_config_free(NULL);
    ehem_cert_install_free(NULL);
    ehem_selftest_free(NULL);
    ehem_attestation_free(NULL);
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
        /* REQ-SYS-007: selftest. */
        cmocka_unit_test(test_selftest_full_shape),
        cmocka_unit_test(test_selftest_minimal_and_missing),
        /* REQ-SYS-011: attestation. */
        cmocka_unit_test(test_attestation_both_shapes),
        cmocka_unit_test(test_attestation_errors),
        /* REQ-SYS-008: shutdown. */
        cmocka_unit_test(test_shutdown_empty_200_drops_cache),
        cmocka_unit_test(test_shutdown_409),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
