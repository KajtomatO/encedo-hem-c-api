/*
 * test_checkin.c — the check-in binding and the automatic expired-certificate
 * recovery, driven offline through the fake transport.
 *
 * verifies: REQ-SYS-003 (3-leg flow: verbatim body relay, cloud leg forced
 *           verified, device legs relaxed only in-flow, typed result),
 *           REQ-NET-005 (auto-recovery: expired-only trigger, opt-out,
 *           single fresh-connection retry, recursion guard, original error
 *           preserved on recovery failure, ehem_cert_refreshed),
 *           REQ-SYS-006 (harvest: newcrt_chain + current_serial from the
 *           leg-2/leg-1 JWT payloads; present, absent, unparseable)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "transport.h"        /* internal: ehem_tls_req_override values */
#include "fake_transport.h"
#include "fixtures/cert_fixture.h"

/* Canned bodies. */
static const char CHALLENGE[] = "{\"check\":\"CHALLENGE-BLOB\"}";
static const char VERIFIED[]  = "{\"checked\":\"CLOUD-VERIFIED-BLOB\"}";
static const char CHECKIN_OK[] =
    "{\"status\":\"ok\",\"newcrt\":\"cert refreshed\",\"newfws\":\"\","
    "\"ignored_future\":1}";
static const char STATUS_MIN[] =
    "{ \"ctx\": 2, \"fls_state\": 0, \"uptime\": 42, \"temp\": 40 }";

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

/* --- explicit check-in (REQ-SYS-003) -------------------------------------- */

static void test_checkin_happy_flow(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_checkin_info *res = NULL;
    assert_int_equal(ehem_system_checkin(ctx, &res), EHEM_OK);
    assert_non_null(res);

    /* Typed result: tolerant parsing, cert_updated derived from newcrt. */
    assert_string_equal(res->status, "ok");
    assert_string_equal(res->newcrt, "cert refreshed");
    assert_true(res->cert_updated);
    assert_string_equal(res->newfws, "");  /* present but empty */
    assert_null(res->newuis);              /* absent */

    /* The three legs, in order, with the mandated TLS posture. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    const fake_captured_request *leg1 = fake_transport_request(fake, 0);
    assert_int_equal(leg1->method, EHEM_HTTP_GET);
    assert_string_equal(leg1->path, "/api/system/checkin");
    assert_int_equal(leg1->tls_override, EHEM_TLS_REQ_RELAX);
    assert_null(leg1->body);

    const fake_captured_request *leg2 = fake_transport_request(fake, 1);
    assert_int_equal(leg2->method, EHEM_HTTP_POST);
    assert_string_equal(leg2->path, EHEM_DEFAULT_CHECKIN_URL);   /* absolute URL */
    assert_int_equal(leg2->tls_override, EHEM_TLS_REQ_VERIFY);   /* forced secure */
    assert_non_null(leg2->body);
    assert_string_equal((const char *)leg2->body, CHALLENGE);    /* verbatim relay */
    assert_string_equal(fake_transport_request_header(fake, 1, "Content-Type"),
                        "application/json");

    const fake_captured_request *leg3 = fake_transport_request(fake, 2);
    assert_int_equal(leg3->method, EHEM_HTTP_POST);
    assert_string_equal(leg3->path, "/api/system/checkin");
    assert_int_equal(leg3->tls_override, EHEM_TLS_REQ_RELAX);
    assert_string_equal((const char *)leg3->body, VERIFIED);     /* verbatim relay */

    /* An explicit check-in is not an "automatic" refresh. */
    assert_false(ehem_cert_refreshed(ctx));

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_checkin_custom_cloud_url(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);

    ehem_options opts;
    ehem_options_init(&opts);
    opts.checkin_url = "https://cloud.example/checkin";
    ehem_ctx *ctx = ctx_with(fake, &opts);

    ehem_checkin_info *res = NULL;
    assert_int_equal(ehem_system_checkin(ctx, &res), EHEM_OK);
    assert_string_equal(fake_transport_request(fake, 1)->path,
                        "https://cloud.example/checkin");

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- certificate harvest (REQ-SYS-006) ------------------------------------ */

/* Leg-2 carries a `newcrt` chain and leg-1 a matching `csn`: both are harvested
 * onto the result while the leg-3 relay stays verbatim. */
static void test_checkin_harvests_chain_and_serial(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECK_CSN_MATCH), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECKED_WITH_NEWCRT), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_checkin_info *res = NULL;
    assert_int_equal(ehem_system_checkin(ctx, &res), EHEM_OK);
    assert_non_null(res);

    /* Harvested cloud-delivered chain (distinct from the leg-3 status string). */
    assert_non_null(res->newcrt_chain);
    assert_string_equal(res->newcrt_chain, EHEM_FX_LEAF_B64);
    assert_string_equal(res->newcrt, "cert refreshed");   /* leg-3 status intact */
    /* Device's current serial from the leg-1 `csn`, normalized hex. */
    assert_non_null(res->current_serial);
    assert_string_equal(res->current_serial, EHEM_FX_LEAF_SERIAL);

    /* Leg-3 body relayed verbatim (the cloud's leg-2 response), untouched by the
     * harvest of its payload. */
    assert_string_equal((const char *)fake_transport_request(fake, 2)->body,
                        EHEM_FX_CHECKED_WITH_NEWCRT);

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* No `newcrt` claim and no `csn`: the harvested fields are NULL and the check-in
 * still succeeds (tolerant). */
static void test_checkin_harvest_absent(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECK_NO_CSN), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECKED_NO_NEWCRT), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_checkin_info *res = NULL;
    assert_int_equal(ehem_system_checkin(ctx, &res), EHEM_OK);
    assert_non_null(res);
    assert_null(res->newcrt_chain);
    assert_null(res->current_serial);

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A leg-2 JWT whose payload segment is undecodable → newcrt_chain NULL, still
 * a successful check-in (harvest never fails the flow). */
static void test_checkin_harvest_unparseable_payload(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECK_CSN_OTHER), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  EHEM_FX_CHECKED_BADPAYLOAD), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_checkin_info *res = NULL;
    assert_int_equal(ehem_system_checkin(ctx, &res), EHEM_OK);
    assert_non_null(res);
    assert_null(res->newcrt_chain);
    /* The leg-1 `csn` still parses even though leg-2's payload did not. */
    assert_non_null(res->current_serial);
    assert_string_equal(res->current_serial, EHEM_FX_OTHER_SERIAL);

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- automatic recovery (REQ-NET-005) ------------------------------------- */

static void test_auto_recovery(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* Original request fails "cert expired" → 3 check-in legs → retry OK. */
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, STATUS_MIN), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_status_info *st = NULL;

    assert_int_equal(ehem_system_status(ctx, &st), EHEM_OK);   /* recovered */
    assert_non_null(st);
    assert_int_equal(st->uptime, 42);
    assert_true(ehem_cert_refreshed(ctx));

    /* Full request sequence. */
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_string_equal(fake_transport_request(fake, 0)->path, "/api/system/status");
    assert_int_equal(fake_transport_request(fake, 0)->tls_override,
                     EHEM_TLS_REQ_DEFAULT);
    assert_string_equal(fake_transport_request(fake, 1)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 2)->path, EHEM_DEFAULT_CHECKIN_URL);
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/system/checkin");
    /* The retry runs under the normal posture but on a fresh connection. */
    const fake_captured_request *retry = fake_transport_request(fake, 4);
    assert_string_equal(retry->path, "/api/system/status");
    assert_int_equal(retry->tls_override, EHEM_TLS_REQ_DEFAULT);
    assert_true(retry->fresh_connection);

    ehem_system_status_free(st);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_auto_recovery_optout(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);

    ehem_options opts;
    ehem_options_init(&opts);
    opts.no_auto_checkin = 1;
    ehem_ctx *ctx = ctx_with(fake, &opts);

    ehem_status_info *st = NULL;
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_ERR_NETWORK);
    assert_null(st);
    assert_int_equal((int)fake_transport_request_count(fake), 1);  /* no extras */
    assert_false(ehem_cert_refreshed(ctx));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Only the expired classification triggers recovery — a generic network/TLS
 * failure does not (REQ-NET-005 trigger scope). */
static void test_non_expired_failure_no_recovery(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(
        fake_transport_push_response(fake, EHEM_ERR_NETWORK, 0, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_status_info *st = NULL;
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_ERR_NETWORK);
    assert_int_equal((int)fake_transport_request_count(fake), 1);
    assert_false(ehem_cert_refreshed(ctx));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Recovery fails → the ORIGINAL error comes back, detail says why. */
static void test_recovery_failure_reports_original(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    fake_transport_set_detail(fake, "certificate has expired");
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);
    /* Check-in leg 1 fails outright. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 500, "boom"), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_status_info *st = NULL;
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_ERR_NETWORK);  /* original */
    assert_null(st);
    assert_false(ehem_cert_refreshed(ctx));
    assert_int_equal((int)fake_transport_request_count(fake), 2);
    assert_non_null(strstr(ehem_last_error(ctx)->message,
                           "automatic check-in recovery failed"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Live-device behavior (found 2026-07-15): the device can accept the cert
 * update (check-in OK) yet keep serving the old certificate until reboot. The
 * retry then still fails "expired" → original error, honest detail, and NO
 * cert_refreshed (the refresh did not take effect). */
static void test_device_still_serves_old_cert(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CHECKIN_OK), 0);
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_status_info *st = NULL;
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_ERR_NETWORK);
    assert_null(st);
    assert_false(ehem_cert_refreshed(ctx));   /* not effective — not reported */
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_non_null(strstr(ehem_last_error(ctx)->message,
                           "still serves the old certificate"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A cert-expired failure INSIDE the check-in must not recurse into another
 * check-in (the in_checkin guard) — it fails the recovery, which reports the
 * original error after exactly two sends. */
static void test_recursion_guard(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);
    assert_int_equal(fake_transport_push_tls_expired(fake, EHEM_ERR_NETWORK), 0);

    ehem_ctx *ctx = ctx_with(fake, NULL);
    ehem_status_info *st = NULL;
    assert_int_equal(ehem_system_status(ctx, &st), EHEM_ERR_NETWORK);
    /* original attempt + check-in leg 1 only — no runaway loop. */
    assert_int_equal((int)fake_transport_request_count(fake), 2);
    assert_false(ehem_cert_refreshed(ctx));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_result_free_null_safe(void **state)
{
    (void)state;
    ehem_checkin_result_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checkin_happy_flow),
        cmocka_unit_test(test_checkin_custom_cloud_url),
        cmocka_unit_test(test_checkin_harvests_chain_and_serial),
        cmocka_unit_test(test_checkin_harvest_absent),
        cmocka_unit_test(test_checkin_harvest_unparseable_payload),
        cmocka_unit_test(test_auto_recovery),
        cmocka_unit_test(test_auto_recovery_optout),
        cmocka_unit_test(test_non_expired_failure_no_recovery),
        cmocka_unit_test(test_recovery_failure_reports_original),
        cmocka_unit_test(test_device_still_serves_old_cert),
        cmocka_unit_test(test_recursion_guard),
        cmocka_unit_test(test_result_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
