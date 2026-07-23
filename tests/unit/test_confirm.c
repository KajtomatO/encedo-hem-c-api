/*
 * test_confirm.c — mobile confirmation engine (REQ-AUTH-009) against a
 * scripted fake broker + fake device. No real sleeping (poll-interval seam).
 *
 * verifies: REQ-AUTH-009 (begin fires session→request→event/new and nothing
 *           else; poll surfaces pending; approval redeems /ext/token and
 *           seeds the token cache under the REQUESTED scope so a subsequent
 *           binding call performs zero acquisitions; deny →
 *           EHEM_ERR_USER_REJECTED terminal without any /ext/token call;
 *           wait times out with EHEM_ERR_CONFIRM_TIMEOUT after ≥1 poll and
 *           leaves the handle usable; transient poll errors are retryable;
 *           terminal-handle misuse → EHEM_ERR_ARG; cancel is leak-free in
 *           every state (ASan))
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
#include "ejwt.h"             /* internal: craft bearer payloads */
#include "proto_ext.h"        /* internal: poll-interval seam */
#include "transport.h"
#include "fake_transport.h"

#define B64_32 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

/* Queue the three begin() legs: session GET, device authreq, event/new. */
static void push_begin(ehem_transport *fake)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-1\"}"), 0);
}

/* Queue an approved event/check + the /ext/token mint with a bearer whose
 * exp claim is far in the future (so the cache entry is fresh). */
static void push_approved(ehem_transport *fake)
{
    char payload[64], seg[128], reply[512];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld}",
                     (long long)9000000000LL);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m,
                                   seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreply\":\"ar.j.w\"}"), 0);
    snprintf(reply, sizeof reply, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply),
                     0);
}

static void test_begin_poll_approved_seeds_cache(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);   /* NEVER logged in — mobile posture */

    push_begin(fake);
    ehem_ext_confirm *c = NULL;
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "system:config",
                                            "my-ctx", "note", &c), EHEM_OK);
    assert_non_null(c);
    assert_int_equal(fake_transport_request_count(fake), 3);
    assert_string_equal(fake_transport_request(fake, 0)->path,
                        EHEM_DEFAULT_NOTIFY_URL "/session");
    assert_string_equal(fake_transport_request(fake, 1)->path,
                        "/api/auth/ext/request");
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        EHEM_DEFAULT_NOTIFY_URL "/event/new");

    /* Pending first. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    ehem_confirm_status status = EHEM_CONFIRM_APPROVED;
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_OK);
    assert_int_equal(status, EHEM_CONFIRM_PENDING);
    assert_string_equal(fake_transport_request(fake, 3)->path,
                        EHEM_DEFAULT_NOTIFY_URL "/event/check/EV-1");

    /* Approved: event/check + /ext/token. */
    push_approved(fake);
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_OK);
    assert_int_equal(status, EHEM_CONFIRM_APPROVED);
    assert_string_equal(fake_transport_request(fake, 5)->path,
                        "/api/auth/ext/token");

    /* The bearer is in the cache under the REQUESTED scope: a binding call
     * for that scope performs exactly ONE request — no acquisition. */
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"devid\":\"d\",\"hostname\":\"h\",\"user\":\"u\"}"), 0);
    ehem_config_info *cfg = NULL;
    assert_int_equal(ehem_system_config(ctx, &cfg), EHEM_OK);
    ehem_system_config_free(cfg);
    assert_int_equal(fake_transport_request_count(fake), before + 1);
    const char *authz = fake_transport_request_header(fake, before,
                                                      "Authorization");
    assert_non_null(authz);
    assert_memory_equal(authz, "Bearer hdr.", 11);

    /* Terminal handle: further polls are misuse. */
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_ERR_ARG);

    ehem_ext_confirm_cancel(c);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_denied_is_terminal_without_token_call(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    push_begin(fake);
    ehem_ext_confirm *c = NULL;
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "keymgmt:list", NULL,
                                            NULL, &c), EHEM_OK);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":true}"), 0);
    ehem_confirm_status status;
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status),
                     EHEM_ERR_USER_REJECTED);
    /* begin(3) + one event/check — NO /ext/token call. */
    assert_int_equal(fake_transport_request_count(fake), 4);
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_ERR_ARG);

    ehem_ext_confirm_cancel(c);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_wait_timeout_and_resume(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_ext_test_set_poll_interval(1);   /* 1 ms — no real waiting */

    push_begin(fake);
    ehem_ext_confirm *c = NULL;
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "s", NULL, NULL, &c),
                     EHEM_OK);

    /* timeout 2 ms, interval 1 ms → polls at elapsed 0/1/2 = exactly 3. */
    for (int i = 0; i < 3; i++) {
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202,
                                                      NULL), 0);
    }
    assert_int_equal(ehem_ext_confirm_wait(ctx, c, 2),
                     EHEM_ERR_CONFIRM_TIMEOUT);
    assert_int_equal(fake_transport_request_count(fake), 6);   /* 3 + 3 */

    /* NOT terminal: resuming works, and an approval completes it. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    push_approved(fake);
    assert_int_equal(ehem_ext_confirm_wait(ctx, c, 1000), EHEM_OK);

    /* A timeout shorter than the interval still polls once. */
    ehem_ext_test_set_poll_interval(0);   /* restore default 5 s */
    push_begin(fake);
    ehem_ext_confirm *c2 = NULL;
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "s2", NULL, NULL, &c2),
                     EHEM_OK);
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(ehem_ext_confirm_wait(ctx, c2, 1),
                     EHEM_ERR_CONFIRM_TIMEOUT);
    /* polls at elapsed 0 and (after a 1 ms step) 1 — the 5 s interval is
     * clamped to the remaining budget, so the wait still takes ~1 ms. */
    assert_int_equal(fake_transport_request_count(fake), before + 2);

    ehem_ext_confirm_cancel(c);
    ehem_ext_confirm_cancel(c2);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_transient_poll_error_is_retryable(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);

    push_begin(fake);
    ehem_ext_confirm *c = NULL;
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "s", NULL, NULL, &c),
                     EHEM_OK);

    /* Broker 500 → error surfaces, handle NOT terminal. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 500,
        "{\"e\":1}"), 0);
    ehem_confirm_status status;
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_ERR_DEVICE);

    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(ehem_ext_confirm_poll(ctx, c, &status), EHEM_OK);
    assert_int_equal(status, EHEM_CONFIRM_PENDING);

    /* A failed begin leaves no handle. */
    ehem_ext_confirm *c2 = NULL;
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 503, NULL), 0);
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, "s", NULL, NULL, &c2),
                     EHEM_ERR_DEVICE);
    assert_null(c2);

    /* Arg contract. */
    assert_int_equal(ehem_ext_confirm_wait(ctx, c, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_ext_confirm_begin(ctx, NULL, NULL, NULL, NULL, &c2),
                     EHEM_ERR_ARG);
    ehem_ext_confirm_cancel(NULL);

    ehem_ext_confirm_cancel(c);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_begin_poll_approved_seeds_cache),
        cmocka_unit_test(test_denied_is_terminal_without_token_call),
        cmocka_unit_test(test_wait_timeout_and_resume),
        cmocka_unit_test(test_transient_poll_error_is_retryable),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
