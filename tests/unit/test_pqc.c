/*
 * test_pqc.c — the ML-KEM encaps/decaps and ML-DSA sign/verify bindings
 * (src/proto_crypto.c) driven offline through the fake transport.
 *
 * verifies: REQ-OPS-007 (encaps body is exactly {kid}, decaps exactly
 *           {kid,ct(b64)}; ss/ct decoded to caller-owned buffers; `alg`
 *           tolerated absent; EHEM_ERR_ARG guards [ct > 1568, malformed kid]
 *           with zero I/O; 400/403/406 mapping; encaps+decaps share ONE
 *           per-KID token),
 *           REQ-OPS-008 (sign body carries {kid,msg(b64)} + ctx only when
 *           given; verify adds sign(b64); sig decoded caller-owned; an
 *           out-of-range verify status [65307, -229] → EHEM_ERR_DEVICE, not
 *           PROTOCOL/crash; EHEM_ERR_ARG guards [sig > 4627, ctx > 255, msg
 *           bounds] with zero I/O; empty-200 verify → EHEM_OK)
 *
 * Helpers mirror test_sign.c / test_cipher.c.
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

/* 32 bytes of 0x41 ('A'). */
#define SS_B64 "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE="
static const uint8_t MSG3[3] = { 0x01, 0x02, 0x03 };   /* b64 "AQID" */
static const uint8_t SIG4[4] = { 0x04, 0x05, 0x0b, 0x0c };

static const char ENCAPS_RESP[] =
    "{\"alg\":\"MLKEM768\",\"ss\":\"" SS_B64 "\",\"ct\":\"BAULDA==\"}";
/* Decaps without alg — the fw quirk makes it unreliable; must be tolerated. */
static const char DECAPS_RESP[] = "{\"ss\":\"" SS_B64 "\"}";
static const char MLDSA_SIGN_RESP[] =
    "{\"alg\":\"MLDSA65\",\"sign\":\"BAULDA==\"}";

/* -------------------------------------------------------------------------- */
/* ML-KEM: bodies, decode, alg tolerance, token share                          */
/* -------------------------------------------------------------------------- */

static void test_mlkem_encaps_decaps_roundtrip(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  ENCAPS_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  DECAPS_RESP), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mlkem_encaps_result *er = NULL;
    ehem_mlkem_secret *ds = NULL;

    assert_int_equal(ehem_mlkem_encaps(ctx, TEST_KID, &er), EHEM_OK);
    assert_non_null(er);
    assert_string_equal(er->alg, "MLKEM768");
    assert_int_equal((int)er->ct_len, 4);
    assert_memory_equal(er->ct, SIG4, 4);
    for (int i = 0; i < EHEM_MLKEM_SS_LEN; i++) {
        assert_int_equal(er->ss[i], 0x41);
    }

    assert_int_equal(ehem_mlkem_decaps(ctx, TEST_KID, er->ct, er->ct_len,
                                       &ds), EHEM_OK);
    assert_non_null(ds);
    assert_string_equal(ds->alg, "");   /* absent alg tolerated */
    assert_memory_equal(ds->ss, er->ss, EHEM_MLKEM_SS_LEN);

    /* 4 requests: ONE token acquisition served encaps and decaps. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/pqc/mlkem/encaps");
    assert_string_equal((const char *)r->body, "{\"kid\":\"" TEST_KID "\"}");
    r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/pqc/mlkem/decaps");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"ct\":\"BAULDA==\"}");

    ehem_mlkem_encaps_result_free(er);
    ehem_mlkem_encaps_result_free(NULL);
    ehem_mlkem_secret_free(ds);
    ehem_mlkem_secret_free(NULL);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_mlkem_bad_ss_is_protocol(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "k2");
    /* ss of the wrong size (4 bytes). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"ss\":\"BAULDA==\",\"ct\":\"BAULDA==\"}"), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mlkem_encaps_result *er = NULL;

    assert_int_equal(ehem_mlkem_encaps(ctx, TEST_KID, &er),
                     EHEM_ERR_PROTOCOL);
    assert_null(er);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* ML-DSA: bodies, decode, empty-200, out-of-range status                      */
/* -------------------------------------------------------------------------- */

static void test_mldsa_sign_and_verify_bodies(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "d1");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  MLDSA_SIGN_RESP), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mldsa_signature *sig = NULL;
    const uint8_t sctx[2] = { 0x66, 0x6f };            /* b64 "Zm8=" */

    assert_int_equal(ehem_mldsa_sign(ctx, TEST_KID, MSG3, sizeof MSG3,
                                     sctx, sizeof sctx, &sig), EHEM_OK);
    assert_non_null(sig);
    assert_string_equal(sig->alg, "MLDSA65");
    assert_int_equal((int)sig->sig_len, 4);
    assert_memory_equal(sig->sig, SIG4, 4);

    assert_int_equal(ehem_mldsa_verify(ctx, TEST_KID, MSG3, sizeof MSG3,
                                       sctx, sizeof sctx,
                                       sig->sig, sig->sig_len), EHEM_OK);
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);

    /* 4 requests: shared token; bodies in the doc's field order. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    const fake_captured_request *r = fake_transport_request(fake, 2);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/pqc/mldsa/sign");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"ctx\":\"Zm8=\"}");
    r = fake_transport_request(fake, 3);
    assert_non_null(r);
    assert_string_equal(r->path, "/api/crypto/pqc/mldsa/verify");
    assert_string_equal((const char *)r->body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"sign\":\"BAULDA==\",\"ctx\":\"Zm8=\"}");

    ehem_mldsa_signature_free(sig);
    ehem_mldsa_signature_free(NULL);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* The fw v1.2.2 failed-verify quirk: raw crypto codes in the status line. */
static void expect_verify_status(long status, ehem_rc want_rc)
{
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "dv");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, NULL),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);

    assert_int_equal(ehem_mldsa_verify(ctx, TEST_KID, MSG3, 3, NULL, 0,
                                       SIG4, 4), want_rc);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_mldsa_verify_out_of_range_status(void **state)
{
    (void)state;
    /* 65307 = SIG_VERIFY_E (-229) printed as unsigned 16-bit; ≥ 400 maps
     * to DEVICE in the shared path already. */
    expect_verify_status(65307, EHEM_ERR_DEVICE);
    /* A raw negative (or sub-200) status must ALSO surface as DEVICE, not
     * PROTOCOL — the device answered, buggily (REQ-OPS-008). */
    expect_verify_status(-229, EHEM_ERR_DEVICE);
    expect_verify_status(100, EHEM_ERR_DEVICE);
    /* The documented mapping still holds. */
    expect_verify_status(406, EHEM_ERR_DEVICE);
    expect_verify_status(403, EHEM_ERR_SCOPE_DENIED);
}

/* -------------------------------------------------------------------------- */
/* pre-validation (no I/O) + encaps device errors                              */
/* -------------------------------------------------------------------------- */

static void test_pqc_arg_guards(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = logged_in_ctx(fake);

    ehem_mlkem_encaps_result *er = NULL;
    ehem_mlkem_secret *ds = NULL;
    ehem_mldsa_signature *sig = NULL;
    static uint8_t big_ct[EHEM_MLKEM_CT_MAX + 1];
    static uint8_t big_sig[EHEM_MLDSA_SIG_MAX + 1];
    static uint8_t big_ctx[EHEM_SIGN_SIG_CTX_MAX + 1];
    static uint8_t big_msg[EHEM_SIGN_MSG_MAX + 1];

    assert_int_equal(ehem_mlkem_encaps(NULL, TEST_KID, &er), EHEM_ERR_ARG);
    assert_int_equal(ehem_mlkem_encaps(ctx, "nope", &er), EHEM_ERR_ARG);
    assert_int_equal(ehem_mlkem_encaps(ctx, TEST_KID, NULL), EHEM_ERR_ARG);

    assert_int_equal(ehem_mlkem_decaps(ctx, TEST_KID, NULL, 4, &ds),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_mlkem_decaps(ctx, TEST_KID, big_ct, 0, &ds),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_mlkem_decaps(ctx, TEST_KID, big_ct, sizeof big_ct,
                                       &ds), EHEM_ERR_ARG);

    assert_int_equal(ehem_mldsa_sign(ctx, TEST_KID, NULL, 3, NULL, 0, &sig),
                     EHEM_ERR_ARG);
    assert_int_equal(ehem_mldsa_sign(ctx, TEST_KID, big_msg, sizeof big_msg,
                                     NULL, 0, &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_mldsa_sign(ctx, TEST_KID, MSG3, 3, big_ctx,
                                     sizeof big_ctx, &sig), EHEM_ERR_ARG);
    assert_int_equal(ehem_mldsa_sign(ctx, TEST_KID, MSG3, 3, NULL, 5, &sig),
                     EHEM_ERR_ARG);

    assert_int_equal(ehem_mldsa_verify(ctx, TEST_KID, MSG3, 3, NULL, 0,
                                       NULL, 4), EHEM_ERR_ARG);
    assert_int_equal(ehem_mldsa_verify(ctx, TEST_KID, MSG3, 3, NULL, 0,
                                       SIG4, 0), EHEM_ERR_ARG);
    assert_int_equal(ehem_mldsa_verify(ctx, TEST_KID, MSG3, 3, NULL, 0,
                                       big_sig, sizeof big_sig),
                     EHEM_ERR_ARG);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_null(er);
    assert_null(ds);
    assert_null(sig);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void expect_encaps_status(ehem_rc want_rc, long status)
{
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "ke");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, NULL),
                     0);

    ehem_ctx *ctx = logged_in_ctx(fake);
    ehem_mlkem_encaps_result *er = NULL;

    assert_int_equal(ehem_mlkem_encaps(ctx, TEST_KID, &er), want_rc);
    assert_null(er);
    assert_int_equal(ehem_last_error(ctx)->http_status, status);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_pqc_device_errors(void **state)
{
    (void)state;
    expect_encaps_status(EHEM_ERR_SCOPE_DENIED, 403);
    expect_encaps_status(EHEM_ERR_DEVICE, 400);
    expect_encaps_status(EHEM_ERR_DEVICE, 406);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mlkem_encaps_decaps_roundtrip),
        cmocka_unit_test(test_mlkem_bad_ss_is_protocol),
        cmocka_unit_test(test_mldsa_sign_and_verify_bodies),
        cmocka_unit_test(test_mldsa_verify_out_of_range_status),
        cmocka_unit_test(test_pqc_arg_guards),
        cmocka_unit_test(test_pqc_device_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
