/*
 * test_notify.c — notification-broker client (REQ-AUTH-008) driven offline
 * through the fake transport.
 *
 * verifies: REQ-AUTH-008 (absolute broker URLs with the default base and a
 *           caller override; TLS forced to VERIFY on every leg; no
 *           Authorization header anywhere; session GET vs POST-with-eid;
 *           register/init shapes; 202 → pending result on both polling
 *           endpoints; register/check 200 {pid,reply}; finalise + event/new
 *           verbatim pass-through bodies; event/check pending/denied/
 *           approved discrimination + unknown-shape PROTOCOL; broker error
 *           payload preserved; frees NULL-safe)
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
#include "transport.h"
#include "fake_transport.h"

#define NOTIFY_BASE "https://api.encedo.com/notify"

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

static void test_session_get_and_post(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    /* Login flow: eid NULL → bodyless GET on the DEFAULT base. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"EPK-B64\"}"), 0);
    char *epk = NULL;
    assert_int_equal(ehem_notify_session(ctx, NULL, NULL, &epk), EHEM_OK);
    assert_string_equal(epk, "EPK-B64");
    ehem_notify_string_free(epk);

    const fake_captured_request *req = fake_transport_request(fake, 0);
    assert_string_equal(req->path, NOTIFY_BASE "/session");
    assert_int_equal(req->method, EHEM_HTTP_GET);
    assert_null(req->body);
    assert_int_equal(req->tls_override, EHEM_TLS_REQ_VERIFY);
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    /* Pairing flow: eid given → POST {"eid": …}; custom base honored. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"EPK2\"}"), 0);
    assert_int_equal(ehem_notify_session(ctx, "https://broker.test/n",
                                         "EID-B64", &epk), EHEM_OK);
    assert_string_equal(epk, "EPK2");
    ehem_notify_string_free(epk);
    req = fake_transport_request(fake, 1);
    assert_string_equal(req->path, "https://broker.test/n/session");
    assert_int_equal(req->method, EHEM_HTTP_POST);
    assert_string_equal((const char *)req->body, "{\"eid\":\"EID-B64\"}");
    assert_int_equal(req->tls_override, EHEM_TLS_REQ_VERIFY);

    /* Broker error: payload preserved. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 500,
        "{\"error\":\"boom\"}"), 0);
    assert_int_equal(ehem_notify_session(ctx, NULL, NULL, &epk),
                     EHEM_ERR_DEVICE);
    assert_null(epk);
    const ehem_error *err = ehem_last_error(ctx);
    assert_int_equal(err->http_status, 500);
    assert_non_null(err->device_payload);
    assert_non_null(strstr(err->device_payload, "boom"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_register_init(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"rid\":\"RID-1\",\"link\":\"https://l.ink/x\"}"), 0);
    ehem_notify_register_info *info = NULL;
    assert_int_equal(ehem_notify_register_init(ctx, NULL, "EPK", "EID",
                                               "REQ.J.WT", &info), EHEM_OK);
    assert_string_equal(info->rid, "RID-1");
    assert_string_equal(info->link, "https://l.ink/x");
    ehem_notify_register_info_free(info);

    const fake_captured_request *req = fake_transport_request(fake, 0);
    assert_string_equal(req->path, NOTIFY_BASE "/register/init");
    assert_string_equal((const char *)req->body,
        "{\"epk\":\"EPK\",\"eid\":\"EID\",\"request\":\"REQ.J.WT\"}");

    /* Missing rid → PROTOCOL. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"link\":\"x\"}"), 0);
    assert_int_equal(ehem_notify_register_init(ctx, NULL, "E", "E", "R",
                                               &info), EHEM_ERR_PROTOCOL);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_register_check_and_finalise(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    /* 202, no body → pending. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    ehem_notify_pairing_reply *r = NULL;
    assert_int_equal(ehem_notify_register_check(ctx, NULL, "RID-1", &r),
                     EHEM_OK);
    assert_true(r->pending);
    assert_null(r->pid);
    assert_null(r->reply);
    ehem_notify_pairing_reply_free(r);
    assert_string_equal(fake_transport_request(fake, 0)->path,
                        NOTIFY_BASE "/register/check/RID-1");

    /* 200 {pid, reply} → completed. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"pid\":\"PID-B64\",\"reply\":\"R.J.W\"}"), 0);
    assert_int_equal(ehem_notify_register_check(ctx, NULL, "RID-1", &r),
                     EHEM_OK);
    assert_false(r->pending);
    assert_string_equal(r->pid, "PID-B64");
    assert_string_equal(r->reply, "R.J.W");
    ehem_notify_pairing_reply_free(r);

    /* 404 (expired/unknown rid) → NOT_FOUND via the shared mapping. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 404, NULL), 0);
    assert_int_equal(ehem_notify_register_check(ctx, NULL, "GONE", &r),
                     EHEM_ERR_NOT_FOUND);
    assert_null(r);

    /* finalise: verbatim {kid, code} pass-through. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, "{}"), 0);
    assert_int_equal(ehem_notify_register_finalise(ctx, NULL, "RID-1",
                                                   "00ff", "Q09ERQ=="),
                     EHEM_OK);
    const fake_captured_request *req = fake_transport_request(fake, 3);
    assert_string_equal(req->path, NOTIFY_BASE "/register/finalise/RID-1");
    assert_string_equal((const char *)req->body,
                        "{\"kid\":\"00ff\",\"code\":\"Q09ERQ==\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_event_new_and_check(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-9\"}"), 0);
    char *eventid = NULL;
    assert_int_equal(ehem_notify_event_new(ctx, NULL, "AQ.RE.Q", "EPK",
                                           &eventid), EHEM_OK);
    assert_string_equal(eventid, "EV-9");

    const fake_captured_request *req = fake_transport_request(fake, 0);
    assert_string_equal(req->path, NOTIFY_BASE "/event/new");
    assert_string_equal((const char *)req->body,
                        "{\"authreq\":\"AQ.RE.Q\",\"epk\":\"EPK\"}");

    /* 202 → pending. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    ehem_notify_event_result *r = NULL;
    assert_int_equal(ehem_notify_event_check(ctx, NULL, eventid, &r), EHEM_OK);
    assert_true(r->pending);
    assert_false(r->denied);
    assert_null(r->authreply);
    ehem_notify_event_result_free(r);
    assert_string_equal(fake_transport_request(fake, 1)->path,
                        NOTIFY_BASE "/event/check/EV-9");

    /* 200 + authreply → approved. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreply\":\"AR.J.W\"}"), 0);
    assert_int_equal(ehem_notify_event_check(ctx, NULL, eventid, &r), EHEM_OK);
    assert_false(r->pending);
    assert_false(r->denied);
    assert_string_equal(r->authreply, "AR.J.W");
    ehem_notify_event_result_free(r);

    /* 200 + deny (any value type) → denied, no authreply expected. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":true}"), 0);
    assert_int_equal(ehem_notify_event_check(ctx, NULL, eventid, &r), EHEM_OK);
    assert_true(r->denied);
    assert_null(r->authreply);
    ehem_notify_event_result_free(r);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":\"user\",\"reason\":\"nope\"}"), 0);
    assert_int_equal(ehem_notify_event_check(ctx, NULL, eventid, &r), EHEM_OK);
    assert_true(r->denied);
    ehem_notify_event_result_free(r);

    /* 200 with neither → unknown broker shape, payload preserved. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"something\":\"else\"}"), 0);
    assert_int_equal(ehem_notify_event_check(ctx, NULL, eventid, &r),
                     EHEM_ERR_PROTOCOL);
    assert_null(r);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "something"));

    ehem_notify_string_free(eventid);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_args_and_frees(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    char *s = NULL;

    assert_int_equal(ehem_notify_session(NULL, NULL, NULL, &s), EHEM_ERR_ARG);
    assert_int_equal(ehem_notify_session(ctx, NULL, NULL, NULL), EHEM_ERR_ARG);
    assert_int_equal(ehem_notify_event_new(ctx, NULL, NULL, "E", &s),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_notify_register_finalise(ctx, NULL, "r", NULL, "c"),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_notify_session(ctx, "", NULL, &s), EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), 0);

    ehem_notify_register_info_free(NULL);
    ehem_notify_pairing_reply_free(NULL);
    ehem_notify_event_result_free(NULL);
    ehem_notify_string_free(NULL);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_get_and_post),
        cmocka_unit_test(test_register_init),
        cmocka_unit_test(test_register_check_and_finalise),
        cmocka_unit_test(test_event_new_and_check),
        cmocka_unit_test(test_args_and_frees),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
