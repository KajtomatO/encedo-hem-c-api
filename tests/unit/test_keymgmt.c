/*
 * test_keymgmt.c — the key-inventory bindings (REQ-KEY-001) driven offline
 * through the fake transport.
 *
 * verifies: REQ-KEY-001
 *   - ehem_key_list: full + minimal entry parse (absent descr → NULL/0),
 *     missing required entry field → EHEM_ERR_PROTOCOL, unknown fields ignored,
 *     descr base64-decoded, scope declaration + path construction, 401/403 per
 *     REQ-AUTH-003, 406/409 → EHEM_ERR_DEVICE with the device payload;
 *   - ehem_key_list_all: multi-page walk terminates on offset >= total; a
 *     mid-walk page with `listed < requested limit` does NOT end the walk early;
 *   - ehem_key_page_free NULL-safe.
 *
 * Every call is authenticated, so it is preceded by a login exchange (challenge
 * GET + token POST) queued via push_login(); the pattern mirrors test_config.c.
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
#include "ehem/keymgmt.h"
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

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

static ehem_ctx *logged_in_ctx(ehem_transport *fake)
{
    ehem_ctx *ctx = ctx_with(fake);
    assert_int_equal(ehem_login(ctx, EJWT_FX_PASSPHRASE), EHEM_OK);
    return ctx;
}

/* -------------------------------------------------------------------------- */
/* single-page list                                                           */
/* -------------------------------------------------------------------------- */

/* "AQID" is std base64 of the 3 bytes {0x01,0x02,0x03}. */
static const char PAGE_FULL[] =
    "{\"offset\":0,\"total\":2,\"listed\":2,\"list\":["
    "{\"kid\":\"09bd0958e1499ecfd51ea62a3f49a84c\",\"created\":1647787070,"
    "\"updated\":1647787099,\"type\":\"ED25519\",\"label\":\"My signing key\","
    "\"descr\":\"AQID\",\"junk\":\"ignored\"},"
    "{\"kid\":\"aabbccddeeff00112233445566778899\",\"type\":\"AES256\","
    "\"created\":1000,\"updated\":2000}"
    "]}";

static void test_list_full_and_minimal_entry(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kl");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, PAGE_FULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_OK);
    assert_non_null(page);

    assert_int_equal((int)page->offset, 0);
    assert_int_equal((int)page->total, 2);
    assert_int_equal((int)page->listed, 2);
    assert_non_null(page->entries);

    /* Entry 0: full — label present, descr base64-decoded, timestamps. */
    const ehem_key_entry *e0 = &page->entries[0];
    assert_string_equal(e0->kid, "09bd0958e1499ecfd51ea62a3f49a84c");
    assert_string_equal(e0->type, "ED25519");
    assert_string_equal(e0->label, "My signing key");
    assert_int_equal((int)e0->descr_len, 3);
    assert_non_null(e0->descr);
    assert_int_equal(e0->descr[0], 0x01);
    assert_int_equal(e0->descr[1], 0x02);
    assert_int_equal(e0->descr[2], 0x03);
    assert_int_equal((int)e0->created, 1647787070);
    assert_int_equal((int)e0->updated, 1647787099);

    /* Entry 1: minimal — no label, no descr → NULL/0. */
    const ehem_key_entry *e1 = &page->entries[1];
    assert_string_equal(e1->kid, "aabbccddeeff00112233445566778899");
    assert_string_equal(e1->type, "AES256");
    assert_null(e1->label);
    assert_null(e1->descr);
    assert_int_equal((int)e1->descr_len, 0);
    assert_int_equal((int)e1->created, 1000);
    assert_int_equal((int)e1->updated, 2000);

    /* The scoped GET carries a bearer and hits the bare list path (offset 0,
     * limit 0 → no path segments); the challenge GET carried no bearer. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_string_equal(fake_transport_request(fake, 2)->path, "/api/keymgmt/list");
    assert_int_equal(fake_transport_request(fake, 2)->method, EHEM_HTTP_GET);
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_null(fake_transport_request_header(fake, 0, "Authorization"));

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* offset/limit both build the path suffix. */
static void test_list_path_with_offset_and_limit(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kp");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":20,\"total\":100,\"listed\":0,\"list\":[]}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 20, 10, &page), EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->offset, 20);
    assert_int_equal((int)page->total, 100);
    assert_int_equal((int)page->listed, 0);
    assert_null(page->entries);

    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/keymgmt/list/20/10");

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* An entry missing a required field (type) → EHEM_ERR_PROTOCOL with detail. */
static void test_list_entry_missing_type(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "km");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":1,\"listed\":1,\"list\":["
        "{\"kid\":\"09bd0958e1499ecfd51ea62a3f49a84c\",\"label\":\"x\"}]}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_ERR_PROTOCOL);
    assert_null(page);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "type"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* An entry missing kid → EHEM_ERR_PROTOCOL (names the field). */
static void test_list_entry_missing_kid(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kk");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"total\":1,\"listed\":1,\"list\":[{\"type\":\"ED25519\"}]}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_ERR_PROTOCOL);
    assert_null(page);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "kid"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 401 with a cached token drives the shared re-acquire + single retry
 * (REQ-AUTH-003): a fresh token then succeeds and the page parses. */
static void test_list_401_reacquire_then_success(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k401a");                             /* req 0,1 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
        "{\"error\":\"revoked\"}"), 0);                    /* req 2: token rejected */
    push_login(fake, "k401b");                             /* req 3,4: re-acquire */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":1,\"listed\":1,\"list\":["
        "{\"kid\":\"09bd0958e1499ecfd51ea62a3f49a84c\",\"type\":\"ED25519\"}]}"),
        0);                                                /* req 5: retry ok */

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 1);

    /* login(2) + scoped(401) + re-login(2) + scoped-retry(200) = 6. */
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_non_null(fake_transport_request_header(fake, 5, "Authorization"));

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 403 (wrong scope) maps to EHEM_ERR_SCOPE_DENIED (REQ-AUTH-003). */
static void test_list_403_scope(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k403");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
        "{\"error\":\"forbidden\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_ERR_SCOPE_DENIED);
    assert_null(page);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 406 (malformed suffix / repo failure) → EHEM_ERR_DEVICE with device payload. */
static void test_list_406_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k406");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406,
        "{\"error\":\"bad suffix\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_ERR_DEVICE);
    assert_null(page);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "bad suffix"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 409 (fls_state / not initialised) → EHEM_ERR_DEVICE. */
static void test_list_409_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k409");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409,
        "{\"error\":\"not initialised\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(ctx, 0, 0, &page), EHEM_ERR_DEVICE);
    assert_null(page);
    assert_int_equal(ehem_last_error(ctx)->http_status, 409);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* full-repository walk                                                        */
/* -------------------------------------------------------------------------- */

/*
 * Three-page walk of a 25-key repo. The device caps pages at 15; we ask for 10.
 * The KEY point (REQ-KEY-001 / OQ-17): page 2 returns `listed:8 < limit:10`, which
 * must NOT end the walk — only offset >= total (or listed 0) does. Pages report
 * listed 10, 8, 7 → offset advances 10, 18, 25; the walk stops when 25 >= 25.
 */
static void push_walk_page(ehem_transport *fake, int total, int start, int count)
{
    char buf[1024];
    int off = snprintf(buf, sizeof buf,
                       "{\"offset\":%d,\"total\":%d,\"listed\":%d,\"list\":[",
                       start, total, count);
    int i;
    for (i = 0; i < count; i++) {
        off += snprintf(buf + off, sizeof buf - (size_t)off,
                        "%s{\"kid\":\"%032x\",\"type\":\"ED25519\"}",
                        i == 0 ? "" : ",", start + i);
    }
    snprintf(buf + off, sizeof buf - (size_t)off, "]}");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, buf), 0);
}

static void test_list_all_multipage_walk(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kall");
    push_walk_page(fake, 25, 0, 10);
    push_walk_page(fake, 25, 10, 8);    /* listed < limit mid-walk: must continue */
    push_walk_page(fake, 25, 18, 7);    /* offset reaches 25 == total → stop */

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list_all(ctx, &page), EHEM_OK);
    assert_non_null(page);

    /* All 25 keys merged; offset 0; total echoed. */
    assert_int_equal((int)page->offset, 0);
    assert_int_equal((int)page->total, 25);
    assert_int_equal((int)page->listed, 25);
    assert_non_null(page->entries);

    /* Order preserved across pages: entry 0 is kid …0000, entry 24 is …0018. */
    char first[33], last[33];
    snprintf(first, sizeof first, "%032x", 0);
    snprintf(last, sizeof last, "%032x", 24);
    assert_string_equal(page->entries[0].kid, first);
    assert_string_equal(page->entries[24].kid, last);

    /* login (2) + three list pages. */
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_string_equal(fake_transport_request(fake, 2)->path, "/api/keymgmt/list/0/10");
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/keymgmt/list/10/10");
    assert_string_equal(fake_transport_request(fake, 4)->path, "/api/keymgmt/list/18/10");

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A single page whose total fits in one request stops after one call. */
static void test_list_all_single_page(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k1");
    push_walk_page(fake, 2, 0, 2);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list_all(ctx, &page), EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 2);
    assert_int_equal((int)page->total, 2);
    /* login (2) + one list page — no needless second request. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* misc                                                                       */
/* -------------------------------------------------------------------------- */

static void test_arg_guards(void **state)
{
    (void)state;
    ehem_key_page *page = NULL;
    assert_int_equal(ehem_key_list(NULL, 0, 0, &page), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_list_all(NULL, &page), EHEM_ERR_ARG);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    assert_int_equal(ehem_key_list(ctx, 0, 0, NULL), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_list_all(ctx, NULL), EHEM_ERR_ARG);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_free_null_safe(void **state)
{
    (void)state;
    ehem_key_page_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_list_full_and_minimal_entry),
        cmocka_unit_test(test_list_path_with_offset_and_limit),
        cmocka_unit_test(test_list_entry_missing_type),
        cmocka_unit_test(test_list_entry_missing_kid),
        cmocka_unit_test(test_list_401_reacquire_then_success),
        cmocka_unit_test(test_list_403_scope),
        cmocka_unit_test(test_list_406_device),
        cmocka_unit_test(test_list_409_device),
        cmocka_unit_test(test_list_all_multipage_walk),
        cmocka_unit_test(test_list_all_single_page),
        cmocka_unit_test(test_arg_guards),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
