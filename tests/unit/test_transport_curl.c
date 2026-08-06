/*
 * test_transport_curl.c — default (libcurl) transport: error translation and
 * option plumbing, exercised offline (no network).
 *
 * verifies: REQ-NET-002 (default transport stands up per context),
 *           REQ-NET-003 (TLS modes settable; insecure is an explicit choice),
 *           REQ-NET-004 / REQ-API-003 (CURLcode → ehem_rc class translation)
 *
 * The CURLcode → ehem_rc mapping is a pure function (ehem_curl_map_error), so
 * it is tested directly against representative libcurl results — deterministic
 * and network-free. Real TLS/connect behavior is exercised at STEP-M1-080/-100.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "context.h"
#include "transport.h"
#include "transport_curl.h"   /* internal, curl-typed: ehem_curl_map_error */

/* Connection-level failures map to the unreachable / network classes the
 * consumer's error table needs to tell apart (REQ-API-003, REQ-NET-004). */
static void test_map_error_classes(void **state)
{
    (void)state;
    assert_int_equal(ehem_curl_map_error(CURLE_OK, 0.0), EHEM_OK);

    /* Never reached the peer → unreachable. */
    assert_int_equal(ehem_curl_map_error(CURLE_COULDNT_RESOLVE_HOST, 0.0),
                     EHEM_ERR_UNREACHABLE);
    assert_int_equal(ehem_curl_map_error(CURLE_COULDNT_CONNECT, 0.0),
                     EHEM_ERR_UNREACHABLE);

    /* Timeout disambiguation via connect_time. */
    assert_int_equal(ehem_curl_map_error(CURLE_OPERATION_TIMEDOUT, 0.0),
                     EHEM_ERR_UNREACHABLE);           /* connect timeout */
    assert_int_equal(ehem_curl_map_error(CURLE_OPERATION_TIMEDOUT, 0.42),
                     EHEM_ERR_NETWORK);               /* total timeout after connect */

    /* Connected then failed (incl. TLS) → network. */
    assert_int_equal(ehem_curl_map_error(CURLE_RECV_ERROR, 0.1), EHEM_ERR_NETWORK);
    assert_int_equal(ehem_curl_map_error(CURLE_SEND_ERROR, 0.1), EHEM_ERR_NETWORK);
    assert_int_equal(ehem_curl_map_error(CURLE_PEER_FAILED_VERIFICATION, 0.1),
                     EHEM_ERR_NETWORK);
    assert_int_equal(ehem_curl_map_error(CURLE_SSL_CONNECT_ERROR, 0.1),
                     EHEM_ERR_NETWORK);

    /* Local/caller errors. */
    assert_int_equal(ehem_curl_map_error(CURLE_OUT_OF_MEMORY, 0.0), EHEM_ERR_NOMEM);
    assert_int_equal(ehem_curl_map_error(CURLE_URL_MALFORMAT, 0.0), EHEM_ERR_ARG);
}

/* A context with no transport override stands up the default (curl) transport,
 * and each TLS mode plumbs through without leaking the handle (ASan). */
static void test_default_transport_per_mode(void **state)
{
    (void)state;
    ehem_ctx *ctx;
    ehem_options opts;

    /* Default options → system trust, default transport created. */
    ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", NULL, &ctx), EHEM_OK);
    assert_non_null(ehem_ctx_transport(ctx));   /* built-in default present */
    ehem_ctx_destroy(ctx);

    /* Explicit insecure mode. */
    ehem_options_init(&opts);
    opts.tls_mode = EHEM_TLS_INSECURE;
    ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://lab.local", &opts, &ctx), EHEM_OK);
    assert_non_null(ehem_ctx_transport(ctx));
    ehem_ctx_destroy(ctx);

    /* Caller CA file. */
    ehem_options_init(&opts);
    opts.tls_mode = EHEM_TLS_CA_FILE;
    opts.ca_file  = "/etc/ssl/certs/ca-certificates.crt";
    ctx = NULL;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    assert_non_null(ehem_ctx_transport(ctx));
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_map_error_classes),
        cmocka_unit_test(test_default_transport_per_mode),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
