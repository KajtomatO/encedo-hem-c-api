/*
 * test_keymgmt.c — the keymgmt bindings (list, search, create, delete, get)
 * driven offline through the fake transport.
 *
 * verifies: REQ-KEY-001, REQ-KEY-002, REQ-KEY-003, REQ-KEY-004, REQ-KEY-005
 *   - ehem_key_list / _list_all: entry parse (absent descr → NULL/0), missing
 *     required field → EHEM_ERR_PROTOCOL, unknown fields ignored, descr decode,
 *     scope + path, 401/403 per REQ-AUTH-003, 406/409 → device; the multi-page
 *     walk terminates on offset >= total, never on listed < limit;
 *   - ehem_key_search / _search_all: anchored descr body bytes, 404 → empty page;
 *   - ehem_key_create: body bytes, label policy, kid parse, 400/406 mapping;
 *   - ehem_key_delete: DELETE + empty-200, malformed kid, 406 → NOT_FOUND;
 *   - ehem_key_get: four response shapes, per-kid scope cache, 406 → NOT_FOUND;
 *   - ehem_key_page_free / ehem_key_details_free NULL-safe.
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

/* Decode the base64url payload segment of an eJWT into a NUL-terminated JSON
 * string (mirrors test_auth.c). */
static void decode_payload(const char *ejwt, char *buf, size_t buf_cap)
{
    const char *d1 = strchr(ejwt, '.');
    const char *d2;
    uint8_t raw[512];
    size_t n;
    assert_non_null(d1);
    d2 = strchr(d1 + 1, '.');
    assert_non_null(d2);
    n = ehem_b64url_decode(d1 + 1, (size_t)(d2 - (d1 + 1)), raw, sizeof raw);
    assert_int_not_equal(n, (size_t)-1);
    assert_true(n < buf_cap);
    memcpy(buf, raw, n);
    buf[n] = '\0';
}

/* Assert the token-acquisition POST at request `token_req` requested `scope`:
 * its body is {"auth":"<ejwt>"} and the eJWT payload carries "scope":"<scope>". */
static void assert_token_scope(ehem_transport *fake, size_t token_req,
                               const char *scope)
{
    const fake_captured_request *r = fake_transport_request(fake, token_req);
    const char *q;
    const char *end;
    char ejwt[700];
    char payload[512];
    char needle[80];
    size_t len;

    assert_non_null(r);
    assert_non_null(r->body);
    q = strstr((const char *)r->body, "\"auth\":\"");
    assert_non_null(q);
    q += 8;                              /* past "auth":" */
    end = strchr(q, '"');
    assert_non_null(end);
    len = (size_t)(end - q);
    assert_true(len < sizeof ejwt);
    memcpy(ejwt, q, len);
    ejwt[len] = '\0';

    decode_payload(ejwt, payload, sizeof payload);
    snprintf(needle, sizeof needle, "\"scope\":\"%s\"", scope);
    assert_non_null(strstr(payload, needle));
}

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"

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
/* create                                                                     */
/* -------------------------------------------------------------------------- */

/* Minimal params: type + label only → body {type,label}; kid parsed back;
 * scope keymgmt:gen; POST to /api/keymgmt/create with a bearer. */
static void test_create_minimal(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kc");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"kid\":\"" TEST_KID "\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "sign key";
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_OK);
    assert_string_equal(kid, TEST_KID);

    const fake_captured_request *post = fake_transport_request(fake, 2);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/keymgmt/create");
    assert_non_null(post->body);
    assert_string_equal((const char *)post->body,
                        "{\"type\":\"ED25519\",\"label\":\"sign key\"}");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_string_equal(fake_transport_request_header(fake, 2, "Content-Type"),
                        "application/json");
    assert_token_scope(fake, 1, "keymgmt:gen");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Full params: mode + descr → body carries all four keys in order, descr is
 * std-base64 of the raw bytes ({0x01,0x02,0x03} → "AQID"). */
static void test_create_full_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kcf");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"kid\":\"" TEST_KID "\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    const uint8_t descr[] = {0x01, 0x02, 0x03};
    ehem_key_create_params p = {0};
    p.type = "SECP256R1";
    p.label = "k";
    p.mode = "ECDH,ExDSA";
    p.descr = descr;
    p.descr_len = sizeof descr;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_OK);

    assert_string_equal((const char *)fake_transport_request(fake, 2)->body,
        "{\"type\":\"SECP256R1\",\"label\":\"k\",\"mode\":\"ECDH,ExDSA\","
        "\"descr\":\"AQID\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A label longer than 31 bytes → EHEM_ERR_ARG with NO transport call. */
static void test_create_label_too_long(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);

    ehem_ctx *ctx = logged_in_ctx(fake);   /* lazy login: no traffic yet */
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "0123456789012345678901234567890123";   /* 34 chars */
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A non-printable byte in the label → EHEM_ERR_ARG with NO transport call. */
static void test_create_label_nonprintable(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "bad\tlabel";               /* tab is not printable */
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device 400 (unsupported type / invalid mode) → EHEM_ERR_DEVICE with payload. */
static void test_create_400_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kc4");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 400,
        "{\"error\":\"unsupported type\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_create_params p = {0};
    p.type = "NOPE";
    p.label = "k";
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 400);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "unsupported type"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device 406 (repo full / write failed) → EHEM_ERR_DEVICE. */
static void test_create_406_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kc6");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406,
        "{\"error\":\"repo full\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "k";
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A 200 reply missing kid → EHEM_ERR_PROTOCOL. */
static void test_create_missing_kid(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kcm");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"ok\":1}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "k";
    char kid[EHEM_KID_HEX_SIZE] = {0};
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_PROTOCOL);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "kid"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_create_arg_guards(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_key_create_params p = {0};
    p.type = "ED25519";
    p.label = "k";
    char kid[EHEM_KID_HEX_SIZE] = {0};

    assert_int_equal(ehem_key_create(NULL, &p, kid), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_create(ctx, NULL, kid), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_create(ctx, &p, NULL), EHEM_ERR_ARG);
    p.type = NULL;
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_ARG);
    p.type = "ED25519"; p.label = NULL;
    assert_int_equal(ehem_key_create(ctx, &p, kid), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* delete                                                                     */
/* -------------------------------------------------------------------------- */

/* Empty-200 → EHEM_OK; sends DELETE + kid path, no body, with a bearer;
 * scope keymgmt:del. */
static void test_delete_empty_200_ok(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kd");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_key_delete(ctx, TEST_KID), EHEM_OK);

    const fake_captured_request *del = fake_transport_request(fake, 2);
    assert_int_equal(del->method, EHEM_HTTP_DELETE);
    assert_string_equal(del->path, "/api/keymgmt/delete/" TEST_KID);
    assert_null(del->body);
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_token_scope(fake, 1, "keymgmt:del");

    /* last-error left clean on success. */
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A non-empty 200 body is tolerated as success too. */
static void test_delete_200_with_body_ok(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kdb");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"status\":\"ok\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_key_delete(ctx, TEST_KID), EHEM_OK);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Malformed kids → EHEM_ERR_ARG with NO transport call. */
static void test_delete_malformed_kid(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    assert_int_equal(ehem_key_delete(ctx, "xyz"), EHEM_ERR_ARG);            /* too short */
    assert_int_equal(ehem_key_delete(ctx, TEST_KID "0"), EHEM_ERR_ARG);    /* 33 chars */
    assert_int_equal(ehem_key_delete(ctx,
        "09bd0958e1499ecfd51ea62a3f49a84g"), EHEM_ERR_ARG);                /* non-hex 'g' */
    assert_int_equal(ehem_key_delete(ctx, NULL), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 406 (kid not in repo) → EHEM_ERR_NOT_FOUND (REQ-KEY-004). */
static void test_delete_406_not_found(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kd6");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406,
        "{\"error\":\"not found\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_key_delete(ctx, TEST_KID), EHEM_ERR_NOT_FOUND);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 403 (wrong scope) → EHEM_ERR_SCOPE_DENIED (REQ-AUTH-003). */
static void test_delete_403_scope(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kd3");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
        "{\"error\":\"forbidden\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_key_delete(ctx, TEST_KID), EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* search                                                                     */
/* -------------------------------------------------------------------------- */

/* base64("EXTAID") == "RVhUQUlE" (the doc's worked example). */
#define PAT     ((const uint8_t *)"EXTAID")
#define PAT_LEN 6
#define PAT_B64 "RVhUQUlE"

static const char SEARCH_HIT[] =
    "{\"offset\":0,\"total\":1,\"listed\":1,\"list\":["
    "{\"kid\":\"" TEST_KID "\",\"type\":\"ED25519\",\"label\":\"paired app\","
    "\"descr\":\"" PAT_B64 "\"}]}";

/* PREFIX → descr "^<b64>"; body {descr,offset,limit} byte-exact; POST to
 * /api/keymgmt/search; scope keymgmt:search; page parses like list. */
static void test_search_prefix_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ks");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, SEARCH_HIT), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 1);
    assert_string_equal(page->entries[0].kid, TEST_KID);

    const fake_captured_request *post = fake_transport_request(fake, 2);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/keymgmt/search");
    assert_string_equal((const char *)post->body,
        "{\"descr\":\"^" PAT_B64 "\",\"offset\":0,\"limit\":15}");
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_token_scope(fake, 1, "keymgmt:search");

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* SUFFIX → descr "<b64>$". */
static void test_search_suffix_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kss");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, SEARCH_HIT), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_SUFFIX, 5, 10, &page),
        EHEM_OK);
    assert_string_equal((const char *)fake_transport_request(fake, 2)->body,
        "{\"descr\":\"" PAT_B64 "$\",\"offset\":5,\"limit\":10}");

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* SUBSTRING → descr "<b64>" (no anchor). */
static void test_search_substring_body(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ksu");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, SEARCH_HIT), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_SUBSTRING, 0, 15, &page),
        EHEM_OK);
    assert_string_equal((const char *)fake_transport_request(fake, 2)->body,
        "{\"descr\":\"" PAT_B64 "\",\"offset\":0,\"limit\":15}");

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device 404 (no match) → EHEM_OK with an empty page; last-error clean. */
static void test_search_404_empty(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k404");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 404,
        "{\"error\":\"not found\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 0);
    assert_null(page->entries);
    assert_int_equal((int)page->total, 0);
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);   /* cleaned */

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 400 (malformed / descr decode fail) → EHEM_ERR_DEVICE with payload. */
static void test_search_400_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ks4");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 400,
        "{\"error\":\"bad descr\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_ERR_DEVICE);
    assert_null(page);
    assert_int_equal(ehem_last_error(ctx)->http_status, 400);
    assert_non_null(strstr(ehem_last_error(ctx)->device_payload, "bad descr"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 410 (repo filter failure) → EHEM_ERR_DEVICE. */
static void test_search_410_device(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ks410");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 410,
        "{\"error\":\"filter failure\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_ERR_DEVICE);
    assert_int_equal(ehem_last_error(ctx)->http_status, 410);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Full-walk search: three pages (listed 10,8,7 of total 25) merge to 25; a
 * mid-walk listed<limit does NOT stop early; body offset advances 0,10,18. */
static void test_search_all_walk(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ksa");
    push_walk_page(fake, 25, 0, 10);
    push_walk_page(fake, 25, 10, 8);
    push_walk_page(fake, 25, 18, 7);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search_all(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, &page),
        EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 25);
    assert_int_equal((int)page->total, 25);

    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_non_null(strstr((const char *)fake_transport_request(fake, 2)->body, "\"offset\":0"));
    assert_non_null(strstr((const char *)fake_transport_request(fake, 3)->body, "\"offset\":10"));
    assert_non_null(strstr((const char *)fake_transport_request(fake, 4)->body, "\"offset\":18"));

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Full-walk search with no match (404 on the first page) → empty page. */
static void test_search_all_no_match(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ksn");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 404, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_page *page = NULL;
    assert_int_equal(
        ehem_key_search_all(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_SUBSTRING, &page),
        EHEM_OK);
    assert_non_null(page);
    assert_int_equal((int)page->listed, 0);
    assert_null(page->entries);
    /* login(2) + one search page only. */
    assert_int_equal((int)fake_transport_request_count(fake), 3);

    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_search_arg_guards(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    ehem_key_page *page = NULL;

    assert_int_equal(
        ehem_key_search(NULL, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_ERR_ARG);
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, 0, 15, NULL),
        EHEM_ERR_ARG);
    /* invalid mode */
    assert_int_equal(
        ehem_key_search(ctx, PAT, PAT_LEN, (ehem_key_search_mode)99, 0, 15, &page),
        EHEM_ERR_ARG);
    /* NULL pattern with non-zero length */
    assert_int_equal(
        ehem_key_search(ctx, NULL, 5, EHEM_KEY_SEARCH_PREFIX, 0, 15, &page),
        EHEM_ERR_ARG);
    assert_int_equal(
        ehem_key_search_all(NULL, PAT, PAT_LEN, EHEM_KEY_SEARCH_PREFIX, &page),
        EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* get (single key by kid)                                                    */
/* -------------------------------------------------------------------------- */

#define KID_A TEST_KID
#define KID_B "aabbccddeeff00112233445566778899"

/* Asymmetric shape: pubkey set, der absent; descr decoded; unknown field
 * ignored; requests exact scope keymgmt:use:<kid>. "AQID"={1,2,3}, "BAUG"={4,5,6}. */
static void test_get_asymmetric(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kg");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"ED25519\",\"pubkey\":\"AQID\",\"updated\":100,"
        "\"descr\":\"BAUG\",\"junk\":\"x\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);
    assert_non_null(d);
    assert_string_equal(d->type, "ED25519");
    assert_int_equal((int)d->updated, 100);
    assert_int_equal((int)d->pubkey_len, 3);
    const uint8_t want_pub[] = {1, 2, 3};
    assert_memory_equal(d->pubkey, want_pub, 3);
    assert_null(d->der);
    assert_int_equal((int)d->der_len, 0);
    assert_int_equal((int)d->descr_len, 3);
    const uint8_t want_descr[] = {4, 5, 6};
    assert_memory_equal(d->descr, want_descr, 3);

    const fake_captured_request *get = fake_transport_request(fake, 2);
    assert_int_equal(get->method, EHEM_HTTP_GET);
    assert_string_equal(get->path, "/api/keymgmt/get/" KID_A);
    assert_non_null(fake_transport_request_header(fake, 2, "Authorization"));
    assert_token_scope(fake, 1, "keymgmt:use:" KID_A);

    ehem_key_details_free(d);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* CERT shape: der set, pubkey absent, no descr. */
static void test_get_cert(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kgc");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"CERT\",\"der\":\"AQID\",\"updated\":200}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);
    assert_string_equal(d->type, "CERT");
    assert_null(d->pubkey);
    assert_int_equal((int)d->der_len, 3);
    assert_null(d->descr);

    ehem_key_details_free(d);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* DER_PKEY shape: der set. */
static void test_get_der_pkey(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kgd");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"DER_PKEY\",\"der\":\"BAUG\",\"updated\":300}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);
    assert_string_equal(d->type, "DER_PKEY");
    assert_null(d->pubkey);
    assert_int_equal((int)d->der_len, 3);

    ehem_key_details_free(d);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Symmetric shape: neither pubkey nor der — no material at all. */
static void test_get_symmetric(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kgs");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"AES256\",\"updated\":400}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);
    assert_string_equal(d->type, "AES256");
    assert_null(d->pubkey);
    assert_null(d->der);
    assert_null(d->descr);
    assert_int_equal((int)d->updated, 400);

    ehem_key_details_free(d);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Missing required 'type' → EHEM_ERR_PROTOCOL. */
static void test_get_missing_type(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kgm");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"pubkey\":\"AQID\",\"updated\":1}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_ERR_PROTOCOL);
    assert_null(d);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "type"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 406 (kid not found) → EHEM_ERR_NOT_FOUND. */
static void test_get_406_not_found(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kg6");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406,
        "{\"error\":\"not found\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_ERR_NOT_FOUND);
    assert_null(d);
    assert_int_equal(ehem_last_error(ctx)->http_status, 406);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* 403 (wrong scope) → EHEM_ERR_SCOPE_DENIED. */
static void test_get_403_scope(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "kg3");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
        "{\"error\":\"forbidden\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_ERR_SCOPE_DENIED);
    assert_int_equal(ehem_last_error(ctx)->http_status, 403);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Per-kid scope cache: two kids → two token acquisitions; a repeat get of the
 * first kid reuses its cached token (no re-login). */
static void test_get_per_kid_scope_cache(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ka");                                    /* req 0,1 (use:A) */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"ED25519\",\"pubkey\":\"AQID\",\"updated\":1}"), 0);   /* req 2 */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"ED25519\",\"pubkey\":\"AQID\",\"updated\":1}"), 0);   /* req 3 (A cached) */
    push_login(fake, "kb");                                    /* req 4,5 (use:B) */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"type\":\"AES256\",\"updated\":2}"), 0);            /* req 6 */

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;

    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);   /* login + get */
    ehem_key_details_free(d); d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_A, &d), EHEM_OK);   /* cache hit → get only */
    ehem_key_details_free(d); d = NULL;
    assert_int_equal(ehem_key_get(ctx, KID_B, &d), EHEM_OK);   /* new scope → login + get */
    ehem_key_details_free(d);

    /* login(2)+getA(1)+getA(1)+login(2)+getB(1) = 7 */
    assert_int_equal((int)fake_transport_request_count(fake), 7);
    assert_token_scope(fake, 1, "keymgmt:use:" KID_A);
    assert_token_scope(fake, 5, "keymgmt:use:" KID_B);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Malformed kid → EHEM_ERR_ARG with no transport call; NULL guards. */
static void test_get_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_key_details *d = NULL;

    assert_int_equal(ehem_key_get(ctx, "short", &d), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_get(ctx, KID_A "0", &d), EHEM_ERR_ARG);   /* 33 chars */
    assert_int_equal(ehem_key_get(NULL, KID_A, &d), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_get(ctx, NULL, &d), EHEM_ERR_ARG);
    assert_int_equal(ehem_key_get(ctx, KID_A, NULL), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

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
    ehem_key_details_free(NULL);
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
        cmocka_unit_test(test_create_minimal),
        cmocka_unit_test(test_create_full_body),
        cmocka_unit_test(test_create_label_too_long),
        cmocka_unit_test(test_create_label_nonprintable),
        cmocka_unit_test(test_create_400_device),
        cmocka_unit_test(test_create_406_device),
        cmocka_unit_test(test_create_missing_kid),
        cmocka_unit_test(test_create_arg_guards),
        cmocka_unit_test(test_delete_empty_200_ok),
        cmocka_unit_test(test_delete_200_with_body_ok),
        cmocka_unit_test(test_delete_malformed_kid),
        cmocka_unit_test(test_delete_406_not_found),
        cmocka_unit_test(test_delete_403_scope),
        cmocka_unit_test(test_search_prefix_body),
        cmocka_unit_test(test_search_suffix_body),
        cmocka_unit_test(test_search_substring_body),
        cmocka_unit_test(test_search_404_empty),
        cmocka_unit_test(test_search_400_device),
        cmocka_unit_test(test_search_410_device),
        cmocka_unit_test(test_search_all_walk),
        cmocka_unit_test(test_search_all_no_match),
        cmocka_unit_test(test_search_arg_guards),
        cmocka_unit_test(test_get_asymmetric),
        cmocka_unit_test(test_get_cert),
        cmocka_unit_test(test_get_der_pkey),
        cmocka_unit_test(test_get_symmetric),
        cmocka_unit_test(test_get_missing_type),
        cmocka_unit_test(test_get_406_not_found),
        cmocka_unit_test(test_get_403_scope),
        cmocka_unit_test(test_get_per_kid_scope_cache),
        cmocka_unit_test(test_get_arg_guards),
        cmocka_unit_test(test_arg_guards),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
