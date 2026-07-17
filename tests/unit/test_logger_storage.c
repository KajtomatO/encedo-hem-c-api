/*
 * test_logger_storage.c — the logger (key/list/get) and storage
 * (unlock/lock) bindings driven offline through the fake transport.
 *
 * verifies: REQ-SYS-009 (logger key strict 32/32/64 base64 decode +
 *           PROTOCOL on deviation; list page parse incl. empty; get returns
 *           the raw body verbatim via the non-JSON path; id validation;
 *           404 → NOT_FOUND for the EPA/unknown cases),
 *           REQ-SYS-010 (storage scope COMPOSITION — the disk index and rw
 *           mode ride in the requested token scope; plain URL paths;
 *           empty-200 success; 406/409 mapping; disk range → ARG no I/O)
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
#include "ehem/logger.h"
#include "ehem/storage.h"
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "ejwt.h"             /* internal: base64url for crafted tokens */
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

/* Assert the token POST at request `token_req` requested `scope` (the eJWT
 * payload carries it) — mirrors test_keymgmt.c. */
static void assert_token_scope(ehem_transport *fake, size_t token_req,
                               const char *scope)
{
    const fake_captured_request *r = fake_transport_request(fake, token_req);
    const char *q;
    const char *end;
    const char *d1;
    const char *d2;
    uint8_t raw[512];
    char payload[512];
    char needle[80];
    size_t len, n;

    assert_non_null(r);
    assert_non_null(r->body);
    q = strstr((const char *)r->body, "\"auth\":\"");
    assert_non_null(q);
    q += 8;
    end = strchr(q, '"');
    assert_non_null(end);
    len = (size_t)(end - q);

    d1 = memchr(q, '.', len);
    assert_non_null(d1);
    d2 = memchr(d1 + 1, '.', (size_t)(end - (d1 + 1)));
    assert_non_null(d2);
    n = ehem_b64url_decode(d1 + 1, (size_t)(d2 - (d1 + 1)), raw, sizeof raw);
    assert_int_not_equal(n, (size_t)-1);
    assert_true(n < sizeof payload);
    memcpy(payload, raw, n);
    payload[n] = '\0';
    snprintf(needle, sizeof needle, "\"scope\":\"%s\"", scope);
    assert_non_null(strstr(payload, needle));
}

/* -------------------------------------------------------------------------- */
/* logger key                                                                 */
/* -------------------------------------------------------------------------- */

/* 32 bytes of 0x01 / 0x02 and 64 bytes of 0x03, all std base64. */
#define B64_32_ONES  "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="
#define B64_32_TWOS  "AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI="
#define B64_64_THREES \
    "AwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAw=="

static void test_logger_key_ok(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "lk");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"key\":\"" B64_32_ONES "\",\"nonce\":\"" B64_32_TWOS "\","
        "\"nonce_signed\":\"" B64_64_THREES "\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_logger_key_info *info = NULL;
    assert_int_equal(ehem_logger_key(ctx, &info), EHEM_OK);
    assert_non_null(info);
    assert_int_equal(info->key[0], 0x01);
    assert_int_equal(info->key[31], 0x01);
    assert_int_equal(info->nonce[0], 0x02);
    assert_int_equal(info->nonce_signed[63], 0x03);

    assert_string_equal(fake_transport_request(fake, 2)->path, "/api/logger/key");
    assert_token_scope(fake, 1, "logger:get");

    ehem_logger_key_free(info);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A 31-byte key (wrong size) → PROTOCOL. */
static void test_logger_key_bad_size(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "lkb");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"key\":\"AQEB\",\"nonce\":\"" B64_32_TWOS "\","
        "\"nonce_signed\":\"" B64_64_THREES "\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_logger_key_info *info = NULL;
    assert_int_equal(ehem_logger_key(ctx, &info), EHEM_ERR_PROTOCOL);
    assert_null(info);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* logger list                                                                */
/* -------------------------------------------------------------------------- */

static void test_logger_list_page_and_empty(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ll");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"total\":3,\"id\":[\"62310b2b\",\"62310bf6\",\"6231f603\"]}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"total\":0,\"id\":[]}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_logger_page *page = NULL;
    assert_int_equal(ehem_logger_list(ctx, 0, &page), EHEM_OK);
    assert_int_equal((int)page->total, 3);
    assert_int_equal((int)page->count, 3);
    assert_string_equal(page->ids[0], "62310b2b");
    assert_string_equal(page->ids[2], "6231f603");
    ehem_logger_page_free(page);
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/logger/list/0");

    page = NULL;
    assert_int_equal(ehem_logger_list(ctx, 20, &page), EHEM_OK);
    assert_int_equal((int)page->count, 0);
    assert_null(page->ids);
    ehem_logger_page_free(page);
    assert_string_equal(fake_transport_request(fake, 3)->path,
                        "/api/logger/list/20");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 404 (EPA build or listing failure) → NOT_FOUND. */
static void test_logger_list_404(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ll4");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 404, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_logger_page *page = NULL;
    assert_int_equal(ehem_logger_list(ctx, 0, &page), EHEM_ERR_NOT_FOUND);
    assert_null(page);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* logger get                                                                 */
/* -------------------------------------------------------------------------- */

static void test_logger_get_raw_and_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    static const char LOG_TEXT[] =
        "{\"e\":1,\"sig\":\"aa\"}\n{\"e\":2,\"sig\":\"bb\"}\n";

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "lg");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  LOG_TEXT), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    uint8_t *data = NULL;
    size_t len = 0;
    assert_int_equal(ehem_logger_get(ctx, "62310b2b", &data, &len), EHEM_OK);
    assert_int_equal((int)len, (int)(sizeof LOG_TEXT - 1));
    assert_memory_equal(data, LOG_TEXT, len);   /* verbatim, unparsed */
    ehem_logger_file_free(data);
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/logger/62310b2b");

    /* Guards: non-hex or path-shaped ids never reach the wire. */
    data = NULL;
    assert_int_equal(ehem_logger_get(ctx, "list", &data, &len), EHEM_ERR_ARG);
    assert_int_equal(ehem_logger_get(ctx, "../cfg", &data, &len), EHEM_ERR_ARG);
    assert_int_equal(ehem_logger_get(ctx, "", &data, &len), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* storage                                                                    */
/* -------------------------------------------------------------------------- */

/* The scope carries the arguments: each (disk, mode) is its own token. */
static void test_storage_scope_composition(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "s0");                                          /* 0,1 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);
    push_login(fake, "s1");                                          /* 3,4 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);
    push_login(fake, "s2");                                          /* 6,7 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    /* Read-only disk 0. */
    assert_int_equal(ehem_storage_unlock(ctx, 0, 0), EHEM_OK);
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/storage/unlock");        /* plain path, no /ro */
    assert_token_scope(fake, 1, "storage:disk0");

    /* Read-write disk 1 — a DIFFERENT scope, so a fresh login. */
    assert_int_equal(ehem_storage_unlock(ctx, 1, 1), EHEM_OK);
    assert_token_scope(fake, 4, "storage:disk1:rw");

    /* Lock disk 1 — the no-mode scope, a third token. */
    assert_int_equal(ehem_storage_lock(ctx, 1), EHEM_OK);
    assert_string_equal(fake_transport_request(fake, 8)->path,
                        "/api/storage/lock");
    assert_token_scope(fake, 7, "storage:disk1");

    assert_int_equal((int)fake_transport_request_count(fake), 9);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_storage_errors_and_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "se");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 409,
        "{\"error\":\"formatting\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    /* Disk range guard → ARG, no I/O. */
    assert_int_equal(ehem_storage_unlock(ctx, 3, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_storage_lock(ctx, -1), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    /* 406 (disk unsupported) and 409 (busy) → DEVICE; same-scope token reuse
     * means only ONE login for both calls. */
    assert_int_equal(ehem_storage_unlock(ctx, 0, 0), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);
    assert_int_equal(ehem_storage_unlock(ctx, 0, 0), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 409);
    assert_int_equal((int)fake_transport_request_count(fake), 4);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_free_null_safe(void **state)
{
    (void)state;
    ehem_logger_key_free(NULL);
    ehem_logger_page_free(NULL);
    ehem_logger_file_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_logger_key_ok),
        cmocka_unit_test(test_logger_key_bad_size),
        cmocka_unit_test(test_logger_list_page_and_empty),
        cmocka_unit_test(test_logger_list_404),
        cmocka_unit_test(test_logger_get_raw_and_guards),
        cmocka_unit_test(test_storage_scope_composition),
        cmocka_unit_test(test_storage_errors_and_guards),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
