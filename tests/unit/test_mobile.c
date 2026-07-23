/*
 * test_mobile.c — mobile login mode (REQ-AUTH-010): ehem_login_mobile +
 * confirm_timeout_ms wired through the ensure-token chokepoint. Scripted
 * fake device + broker; the poll-interval seam keeps waits at ~1 ms.
 *
 * verifies: REQ-AUTH-010 (a binding call on a cache miss runs the full
 *           confirm flow and succeeds end-to-end; a cache hit performs no
 *           broker traffic; rejected/timeout surface from the binding call;
 *           passphrase↔mobile switching scrubs the losing mode and the last
 *           call wins; logout ends the mobile session; the 401 retry
 *           composes with (does not multiply) the mobile acquisition; the
 *           pairing trio fails fast with no push; confirm_timeout_ms
 *           default/override honored)
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
#include "ejwt.h"
#include "proto_ext.h"
#include "transport.h"
#include "fake_transport.h"

#define B64_32 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define CONFIG_OK "{\"devid\":\"d\",\"hostname\":\"h\",\"user\":\"u\"}"

static ehem_ctx *ctx_with_opts(ehem_transport *fake, long confirm_timeout_ms)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    opts.confirm_timeout_ms = confirm_timeout_ms;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

/* Queue a full successful mobile acquisition: session, authreq, event/new,
 * event/check(approved), /ext/token. */
static void push_acquisition(ehem_transport *fake)
{
    char payload[64], seg[128], reply[512];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld}",
                     (long long)9000000000LL);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m,
                                   seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-1\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreply\":\"ar.j.w\"}"), 0);
    snprintf(reply, sizeof reply, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply),
                     0);
}

static void test_binding_triggers_confirm_flow(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with_opts(fake, 0);
    ehem_ext_test_set_poll_interval(1);

    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);

    /* Miss → 5 acquisition legs + the config GET itself. */
    push_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CONFIG_OK), 0);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    ehem_system_config_free(cfg);
    cfg = NULL;
    assert_int_equal(fake_transport_request_count(fake), 6);
    assert_string_equal(fake_transport_request(fake, 5)->path,
                        "/api/system/config");
    assert_non_null(fake_transport_request_header(fake, 5, "Authorization"));

    /* Hit → exactly one request, same bearer, zero broker traffic. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CONFIG_OK), 0);
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    ehem_system_config_free(cfg);
    assert_int_equal(fake_transport_request_count(fake), 7);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_rejected_and_timeout_surface_from_binding(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with_opts(fake, 2);   /* 2 ms override */
    ehem_ext_test_set_poll_interval(1);

    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);

    /* Deny on the phone → USER_REJECTED from the binding call. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-1\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":true}"), 0);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg),
                     EHEM_ERR_USER_REJECTED);
    assert_null(cfg);

    /* Unanswered → CONFIRM_TIMEOUT from the binding call (timeout 2 ms,
     * interval 1 ms → exactly 3 event/check polls). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-2\"}"), 0);
    for (int i = 0; i < 3; i++) {
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202,
                                                      NULL), 0);
    }
    assert_int_equal(ehem_system_config(ctx, &cfg),
                     EHEM_ERR_CONFIRM_TIMEOUT);
    assert_null(cfg);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_mode_switching_and_logout(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with_opts(fake, 0);
    ehem_ext_test_set_poll_interval(1);

    /* Passphrase first, then mobile: the mobile path runs (no challenge GET
     * — its first leg is the broker session, an absolute URL). */
    assert_int_equal(ehem_login(ctx, "secret"), EHEM_OK);
    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);
    push_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CONFIG_OK), 0);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    ehem_system_config_free(cfg);
    cfg = NULL;
    assert_string_equal(fake_transport_request(fake, 0)->path,
                        EHEM_DEFAULT_NOTIFY_URL "/session");

    /* Mobile → passphrase: cache dropped, next call is the eJWT exchange
     * (challenge GET fails here — proving the passphrase path runs). */
    assert_int_equal(ehem_login(ctx, "secret"), EHEM_OK);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 503, NULL), 0);
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_ERR_DEVICE);
    size_t n = fake_transport_request_count(fake);
    assert_string_equal(fake_transport_request(fake, n - 1)->path,
                        "/api/auth/token");

    /* Logout ends the mobile session: no credential, no network. */
    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);
    assert_int_equal(ehem_logout(ctx), EHEM_OK);
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_ERR_AUTH_EXPIRED);
    assert_int_equal(fake_transport_request_count(fake), n);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_401_retry_composes_with_mobile(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with_opts(fake, 0);
    ehem_ext_test_set_poll_interval(1);

    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);

    /* First acquisition succeeds; the device then 401s the config GET
     * (token invalidated server-side): the sanctioned retry re-acquires via
     * ONE new confirm flow and retries the GET once. */
    push_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401, NULL), 0);
    push_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CONFIG_OK), 0);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    ehem_system_config_free(cfg);
    /* 5 + 1 (401) + 5 + 1 (retry) = 12 — one retry, no multiplication. */
    assert_int_equal(fake_transport_request_count(fake), 12);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_pairing_trio_fails_fast_in_mobile_mode(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with_opts(fake, 0);

    assert_int_equal(ehem_login_mobile(ctx), EHEM_OK);

    ehem_ext_init_info *init = NULL;
    ehem_ext_validate_info *val = NULL;
    ehem_ext_mac_info *mac = NULL;
    assert_int_equal(ehem_ext_init(ctx, B64_32, &init),
                     EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(ehem_ext_validate(ctx, B64_32, "r.j.w", &val),
                     EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(ehem_ext_mac(ctx, B64_32, &mac), EHEM_ERR_SCOPE_DENIED);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "no push was sent"));
    assert_int_equal(fake_transport_request_count(fake), 0);
    assert_null(init);
    assert_null(val);
    assert_null(mac);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_binding_triggers_confirm_flow),
        cmocka_unit_test(test_rejected_and_timeout_surface_from_binding),
        cmocka_unit_test(test_mode_switching_and_logout),
        cmocka_unit_test(test_401_retry_composes_with_mobile),
        cmocka_unit_test(test_pairing_trio_fails_fast_in_mobile_mode),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
