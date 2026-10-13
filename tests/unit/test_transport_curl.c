/*
 * test_transport_curl.c — default (libcurl) transport: error translation and
 * option plumbing, exercised offline (no network).
 *
 * verifies: REQ-NET-002 (default transport stands up per context),
 *           REQ-NET-003 (TLS modes settable; insecure is an explicit choice),
 *           REQ-NET-004 / REQ-API-003 (CURLcode → ehem_rc class translation),
 *           REQ-NET-005 (the expired-certificate trigger classifies ONLY an
 *           expired peer certificate — not-yet-valid and every other
 *           verification failure stay unclassified; rev 2, STEP-M12-030)
 *
 * The CURLcode → ehem_rc mapping and the expired-certificate verdict are pure
 * functions (ehem_curl_map_error, ehem_curl_tls_expired), so they are tested
 * directly against representative libcurl results — deterministic and
 * network-free. Real TLS/connect behavior is exercised at STEP-M1-080/-100;
 * the release job's scripts/release/check-expired-classifier.sh proves both
 * verdicts against a real wolfSSL handshake.
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

/* REQ-NET-005: the expired-certificate trigger, fed the verify results and
 * error texts libcurl reports with each TLS backend. Measured 2026-10-13
 * against local endpoints: libcurl 8.5.0 + OpenSSL 3.0.13 → verify result 10 /
 * 9 with "SSL certificate problem: certificate has expired" / "... is not yet
 * valid"; libcurl 8.10.1 + wolfSSL 5.7.2 (the release pin) → verify result 0
 * for both, "server verification failed: certificate has expired." / "...
 * certificate not valid yet." — there the text decides. wolfSSL's own codes
 * (-151 ASN_AFTER_DATE_E, -150 ASN_BEFORE_DATE_E) and texts cover a libcurl
 * that surfaces them. Expired → 1; NOT YET VALID → 0 (rev 2); every other
 * verification failure → 0. */
static void test_tls_expired_classifier(void **state)
{
    (void)state;
    const CURLcode pfv = CURLE_PEER_FAILED_VERIFICATION;
    const CURLcode sce = CURLE_SSL_CONNECT_ERROR;

    /* Expired: verify result first, the text as the fallback. */
    assert_int_equal(ehem_curl_tls_expired(pfv, 10,
        "SSL certificate problem: certificate has expired"), 1);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0,
        "server verification failed: certificate has expired."), 1);
    assert_int_equal(ehem_curl_tls_expired(sce, -151,
        "SSL_connect failed with error -151: ASN date error, current date after"), 1);
    assert_int_equal(ehem_curl_tls_expired(sce, 0,
        "SSL_connect failed with error -151: ASN date error, current date after"), 1);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0, "ASN_AFTER_DATE_E"), 1);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0, "certificate has expired"), 1);
    assert_int_equal(ehem_curl_tls_expired(pfv, 10, NULL), 1);

    /* Not yet valid is NOT expired — by code, by text, and the code wins
     * over any text. */
    assert_int_equal(ehem_curl_tls_expired(sce, -150,
        "SSL_connect failed with error -150: ASN date error, current date before"), 0);
    assert_int_equal(ehem_curl_tls_expired(sce, 0,
        "SSL_connect failed with error -150: ASN date error, current date before"), 0);
    assert_int_equal(ehem_curl_tls_expired(pfv, 9,
        "SSL certificate problem: certificate is not yet valid"), 0);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0,
        "server verification failed: certificate not valid yet."), 0);
    assert_int_equal(ehem_curl_tls_expired(pfv, 9, "expired"), 0);
    assert_int_equal(ehem_curl_tls_expired(sce, -150, "expired"), 0);

    /* Other verification failures stay unclassified. */
    assert_int_equal(ehem_curl_tls_expired(pfv, 18,
        "SSL certificate problem: self-signed certificate"), 0);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0,
        "SSL: no alternative certificate subject name matches target host name 'x'"), 0);
    assert_int_equal(ehem_curl_tls_expired(sce, -188,
        "SSL_connect failed with error -188: ASN no signer error to confirm failure"), 0);
    assert_int_equal(ehem_curl_tls_expired(pfv, 0, NULL), 0);

    /* Only TLS verification codes classify at all. */
    assert_int_equal(ehem_curl_tls_expired(CURLE_COULDNT_CONNECT, 10, "expired"), 0);
    assert_int_equal(ehem_curl_tls_expired(CURLE_OPERATION_TIMEDOUT, -151, NULL), 0);
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
        cmocka_unit_test(test_tls_expired_classifier),
        cmocka_unit_test(test_default_transport_per_mode),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
