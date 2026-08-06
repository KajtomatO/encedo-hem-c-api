/*
 * test_random.c — the ehem_random encrypt-IV harvest (src/proto_crypto.c)
 * driven offline through the fake transport.
 *
 * verifies: REQ-OPS-002 (len 1..48 issues ⌈len/16⌉ AES128-CBC encrypt
 *           requests on a single-zero-byte payload; the concatenated
 *           response IVs fill buf exactly incl. the partial tail; zero len
 *           or NULL args → EHEM_ERR_ARG with no I/O; an IV-less [ECB-shaped]
 *           response → EHEM_ERR_PROTOCOL; one token serves all round-trips)
 *
 * Helpers mirror test_cipher.c.
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
#include "ehem/crypto.h"
#include "proto_auth.h"
#include "ejwt.h"
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

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"

/* Push one CBC-shaped encrypt response whose 16-byte IV is `fill` repeated. */
static void push_cbc_iv(ehem_transport *fake, uint8_t fill)
{
    uint8_t iv[16];
    char iv_b64[32];
    char resp[128];
    size_t n;

    memset(iv, fill, sizeof iv);
    n = ehem_b64_std_encode(iv, sizeof iv, iv_b64, sizeof iv_b64);
    assert_int_not_equal(n, (size_t)-1);
    snprintf(resp, sizeof resp,
             "{\"ciphertext\":\"BAULDA==\",\"iv\":\"%s\"}", iv_b64);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

static void test_random_concat_and_tail(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "r1");
    push_cbc_iv(fake, 0xA1);
    push_cbc_iv(fake, 0xB2);
    push_cbc_iv(fake, 0xC3);

    ehem_ctx *ctx = logged_in_ctx(fake);
    uint8_t buf[40];

    /* 40 bytes → 3 round-trips; the third IV is used for 8 bytes only. */
    assert_int_equal(ehem_random(ctx, TEST_KID, buf, sizeof buf), EHEM_OK);
    for (int i = 0; i < 16; i++)  assert_int_equal(buf[i], 0xA1);
    for (int i = 16; i < 32; i++) assert_int_equal(buf[i], 0xB2);
    for (int i = 32; i < 40; i++) assert_int_equal(buf[i], 0xC3);

    /* login (2) + 3 encrypts = 5; ONE token served all round-trips. */
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/cipher/encrypt");
    /* The throwaway payload: one zero byte ("AA==") under AES128-CBC. */
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AA==\","
                        "\"alg\":\"AES128-CBC\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_single_and_exact_block(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "r2");
    push_cbc_iv(fake, 0x5A);
    push_cbc_iv(fake, 0x11);

    ehem_ctx *ctx = logged_in_ctx(fake);
    uint8_t one;
    uint8_t block[16];

    /* len 1 → one request, first IV byte. */
    assert_int_equal(ehem_random(ctx, TEST_KID, &one, 1), EHEM_OK);
    assert_int_equal(one, 0x5A);
    /* len 16 → exactly one more request. */
    assert_int_equal(ehem_random(ctx, TEST_KID, block, sizeof block), EHEM_OK);
    for (int i = 0; i < 16; i++) assert_int_equal(block[i], 0x11);
    assert_int_equal((int)fake_transport_request_count(fake), 4);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_arg_guards_and_no_iv(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);
    uint8_t buf[8];

    assert_int_equal(ehem_random(NULL, TEST_KID, buf, 8), EHEM_ERR_ARG);
    assert_int_equal(ehem_random(ctx, NULL, buf, 8), EHEM_ERR_ARG);
    assert_int_equal(ehem_random(ctx, "nope", buf, 8), EHEM_ERR_ARG);
    assert_int_equal(ehem_random(ctx, TEST_KID, NULL, 8), EHEM_ERR_ARG);
    assert_int_equal(ehem_random(ctx, TEST_KID, buf, 0), EHEM_ERR_ARG);
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);

    /* An IV-less (ECB-shaped) response is a PROTOCOL failure — the harvest
     * source disappeared. */
    fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "r3");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"ciphertext\":\"BAULDA==\"}"),
                     0);
    ctx = logged_in_ctx(fake);
    assert_int_equal(ehem_random(ctx, TEST_KID, buf, 8), EHEM_ERR_PROTOCOL);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_random_concat_and_tail),
        cmocka_unit_test(test_random_single_and_exact_block),
        cmocka_unit_test(test_random_arg_guards_and_no_iv),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
