/*
 * test_transport.c — the transport vtable seam, exercised offline through the
 * fake transport injected via context options.
 *
 * verifies: REQ-NET-001 (all I/O behind an injectable vtable; the override is
 *           honored; per-request timeouts travel in the request),
 *           REQ-TEST-001 (a request flows ctx → vtable → fake with no network)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "context.h"          /* internal: ehem_ctx_transport */
#include "transport.h"        /* internal: ehem_request/response, send */
#include "fake_transport.h"

/* Create a context whose transport is the given fake override. */
static ehem_ctx *ctx_with_fake(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    assert_non_null(ctx);
    /* The context must expose exactly the injected transport (REQ-NET-001). */
    assert_ptr_equal(ehem_ctx_transport(ctx), fake);
    return ctx;
}

/* A GET flows through the seam; the response comes back and the outgoing
 * request (method, path, header) is captured for assertion. */
static void test_get_roundtrip_both_directions(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(
        fake_transport_push_response(fake, EHEM_OK, 200, "{\"ok\":true}"), 0);

    ehem_ctx *ctx = ctx_with_fake(fake);
    const ehem_transport *t = ehem_ctx_transport(ctx);

    const ehem_header headers[] = {
        { "Accept", "application/json" },
    };
    ehem_request req;
    memset(&req, 0, sizeof req);
    req.method             = EHEM_HTTP_GET;
    req.path               = "/api/system/status";
    req.headers            = headers;
    req.header_count       = 1;
    req.connect_timeout_ms = 4000;
    req.total_timeout_ms   = 9000;

    ehem_response resp;
    memset(&resp, 0, sizeof resp);
    assert_int_equal(ehem_transport_send(t, &req, &resp), EHEM_OK);

    /* Incoming direction. */
    assert_int_equal(resp.status, 200);
    assert_non_null(resp.body);
    assert_string_equal((const char *)resp.body, "{\"ok\":true}");
    assert_int_equal((int)resp.body_len, (int)strlen("{\"ok\":true}"));
    ehem_response_free(&resp);
    /* Double-free / freed-struct is safe. */
    ehem_response_free(&resp);

    /* Outgoing direction. */
    assert_int_equal((int)fake_transport_request_count(fake), 1);
    const fake_captured_request *cap = fake_transport_request(fake, 0);
    assert_non_null(cap);
    assert_int_equal(cap->method, EHEM_HTTP_GET);
    assert_string_equal(cap->path, "/api/system/status");
    assert_null(cap->body);
    assert_string_equal(fake_transport_request_header(fake, 0, "Accept"),
                        "application/json");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A POST body is carried through and captured byte-for-byte. */
static void test_post_body_captured(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 201, "created"), 0);

    ehem_ctx *ctx = ctx_with_fake(fake);
    const char *payload = "{\"auth\":\"eJWT\"}";

    ehem_request req;
    memset(&req, 0, sizeof req);
    req.method   = EHEM_HTTP_POST;
    req.path     = "/api/auth/token";
    req.body     = (const uint8_t *)payload;
    req.body_len = strlen(payload);

    ehem_response resp;
    memset(&resp, 0, sizeof resp);
    assert_int_equal(ehem_transport_send(ehem_ctx_transport(ctx), &req, &resp), EHEM_OK);
    assert_int_equal(resp.status, 201);
    ehem_response_free(&resp);

    const fake_captured_request *cap = fake_transport_request(fake, 0);
    assert_non_null(cap);
    assert_int_equal(cap->method, EHEM_HTTP_POST);
    assert_non_null(cap->body);
    assert_int_equal((int)cap->body_len, (int)strlen(payload));
    assert_memory_equal(cap->body, payload, cap->body_len);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A scripted transport-level failure surfaces as its error rc with a zeroed
 * response — the class distinction REQ-API-003 needs from the transport. */
static void test_transport_error_passthrough(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    assert_int_equal(
        fake_transport_push_response(fake, EHEM_ERR_UNREACHABLE, 0, NULL), 0);

    ehem_ctx *ctx = ctx_with_fake(fake);

    ehem_request req;
    memset(&req, 0, sizeof req);
    req.method = EHEM_HTTP_GET;
    req.path   = "/api/system/version";

    ehem_response resp;
    memset(&resp, 0xAB, sizeof resp);   /* poison to prove send() zeroes it */
    assert_int_equal(ehem_transport_send(ehem_ctx_transport(ctx), &req, &resp),
                     EHEM_ERR_UNREACHABLE);
    assert_int_equal(resp.status, 0);
    assert_null(resp.body);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Dispatch guards: NULL transport / NULL args are argument errors, not crashes. */
static void test_send_arg_guards(void **state)
{
    (void)state;
    ehem_request req;
    ehem_response resp;
    memset(&req, 0, sizeof req);
    memset(&resp, 0, sizeof resp);
    assert_int_equal(ehem_transport_send(NULL, &req, &resp), EHEM_ERR_ARG);
    ehem_transport_destroy(NULL);   /* NULL-safe */
    ehem_response_free(NULL);       /* NULL-safe */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_get_roundtrip_both_directions),
        cmocka_unit_test(test_post_body_captured),
        cmocka_unit_test(test_transport_error_passthrough),
        cmocka_unit_test(test_send_arg_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
