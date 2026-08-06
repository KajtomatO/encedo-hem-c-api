/*
 * test_ext_login_live.c — the complete ExtAuth login round-trip against the
 * real device via the simulated authenticator: pair → request → decrypt our
 * scheme-A scope entry → countersign → token → USE the bearer. No broker, no
 * phone (REQ-TEST-006). Closes the core of ARCHITECTURE §12 risk 6.
 *
 * verifies: REQ-AUTH-007 (request: reachable with NO Authorization on a
 *           never-logged-in context; authreq signature verifies with
 *           ECDH(our ephemeral, eid); iss/aud/jti/iat/exp claims; exp−iat =
 *           3600 s for a plain scope and 900 s for a keymgmt:use:<kid>
 *           scope, whose decrypted entry carries the "#<base64{t,l}>"
 *           rewrite; ctx/note echoed; the scope object is keyed by
 *           base64(pid) and its entry decrypts+authenticates with K =
 *           HMAC(ECDH(identity, eid), jti); token: the issued bearer's
 *           sub = base64(kid), scope #-stripped, exp = the authreply's exp;
 *           the bearer AUTHENTICATES a real GET /api/system/config; a
 *           tampered authreply → EHEM_ERR_AUTH_FAILED with HTTP 401),
 *           REQ-TEST-006 (the full simulated login cycle runs unattended
 *           with zero broker traffic)
 *
 * Internal-linking exception (test_auth_live precedent): needs the crypto
 * shim + ext_sim, and drives the transport directly for the bearer-use leg.
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
#include "context.h"          /* internal: ctx->transport for the bearer leg */
#include "crypto_shim.h"
#include "ejwt.h"             /* internal: base64url decode (bearer payload) */
#include "ext_sim.h"
#include "json.h"
#include "transport.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

#define SIM_LABEL "EHEMTEST-extsim-login"

static ehem_ctx *g_ctx;
static ehem_test_keyreg g_reg;

/* Pair the simulated authenticator; returns the kid (tracked for cleanup)
 * and fills identity/eid_raw/pid_b64 (caller frees pid_b64). */
static char *pair_sim(ehem_sim_keypair *identity, uint8_t eid_raw[32],
                      char **pid_b64_out)
{
    ehem_sim_keypair eph, confirm, pid_src;
    assert_int_equal(ehem_sim_keypair_gen(&eph), 0);
    assert_int_equal(ehem_sim_keypair_gen(identity), 0);
    assert_int_equal(ehem_sim_keypair_gen(&confirm), 0);
    assert_int_equal(ehem_sim_keypair_gen(&pid_src), 0);

    char *epk_b64 = ehem_sim_b64(eph.pub, 32);
    ehem_ext_init_info *init = NULL;
    assert_int_equal(ehem_ext_init(g_ctx, epk_b64, &init), EHEM_OK);
    assert_int_equal(ehem_sim_b64_key(init->eid, eid_raw), 0);

    uint8_t secret[32], jti_raw[32];
    assert_int_equal(ehem_sim_shared(&eph, eid_raw, secret), 0);
    ehem_json *payload = ehem_sim_jwt_open(init->request, secret);
    assert_non_null(payload);
    const char *jti_b64 = NULL;
    assert_true(ehem_json_get_string(payload, "jti", &jti_b64));
    assert_int_equal(ehem_sim_b64_key(jti_b64, jti_raw), 0);

    char *jti_copy = ehem_sim_b64(jti_raw, 32);
    char *iss_b64 = ehem_sim_b64(identity->pub, 32);
    char *cepk_b64 = ehem_sim_b64(confirm.pub, 32);
    ehem_json *claims = ehem_json_new_object();
    assert_non_null(claims);
    assert_true(ehem_json_add_string(claims, "jti", jti_copy));
    assert_true(ehem_json_add_string(claims, "iss", iss_b64));
    assert_true(ehem_json_add_string(claims, "label", SIM_LABEL));
    assert_true(ehem_json_add_string(claims, "epk", cepk_b64));
    uint8_t sign_key[32];
    assert_int_equal(ehem_sim_shared(identity, eid_raw, sign_key), 0);
    char *reply = NULL;
    assert_int_equal(ehem_sim_jwt_build(claims, sign_key, &reply), 0);

    char *pid_b64 = ehem_sim_b64(pid_src.pub, 32);
    ehem_ext_validate_info *val = NULL;
    assert_int_equal(ehem_ext_validate(g_ctx, pid_b64, reply, &val), EHEM_OK);
    char *kid = val->kid;
    val->kid = NULL;   /* steal */
    ehem_test_track(&g_reg, kid);

    ehem_json_free(payload);
    ehem_json_free(claims);
    ehem_ext_validate_free(val);
    ehem_ext_init_free(init);
    free(epk_b64);
    free(jti_copy);
    free(iss_b64);
    free(cepk_b64);
    free(reply);
    *pid_b64_out = pid_b64;
    return kid;
}

/* Run one /ext/request on `req_ctx`, verify the authreq signature with
 * ECDH(fresh ephemeral, eid), and return the parsed payload + raw jti. */
static ehem_json *run_request(ehem_ctx *req_ctx, const char *scope,
                              const char *ctx_str, const char *note,
                              const uint8_t eid_raw[32], uint8_t jti_raw[32])
{
    ehem_sim_keypair eph;
    assert_int_equal(ehem_sim_keypair_gen(&eph), 0);
    char *epk_b64 = ehem_sim_b64(eph.pub, 32);

    ehem_ext_request_info *info = NULL;
    assert_int_equal(ehem_ext_request(req_ctx, epk_b64, scope, ctx_str, note,
                                      &info), EHEM_OK);
    assert_non_null(info);
    assert_string_equal(info->epk, epk_b64);

    uint8_t secret[32];
    assert_int_equal(ehem_sim_shared(&eph, eid_raw, secret), 0);
    ehem_json *payload = ehem_sim_jwt_open(info->authreq, secret);
    assert_non_null(payload);   /* signature valid → construction proven */

    const char *aud = NULL, *jti_b64 = NULL;
    assert_true(ehem_json_get_string(payload, "aud", &aud));
    assert_string_equal(aud, epk_b64);
    assert_true(ehem_json_get_string(payload, "jti", &jti_b64));
    assert_int_equal(ehem_sim_b64_key(jti_b64, jti_raw), 0);

    ehem_ext_request_free(info);
    free(epk_b64);
    return payload;
}

/* Fetch + decrypt this authenticator's scope-object entry. Caller frees. */
static char *decrypt_own_entry(const ehem_json *payload, const char *pid_b64,
                               const ehem_sim_keypair *identity,
                               const uint8_t eid_raw[32],
                               const uint8_t jti_raw[32])
{
    const ehem_json *scope_obj = ehem_json_get(payload, "scope");
    assert_true(ehem_json_is_object(scope_obj));
    const ehem_json *entry = ehem_json_get(scope_obj, pid_b64);
    assert_non_null(entry);
    const char *value = NULL;
    assert_true(ehem_json_as_string(entry, &value));

    uint8_t ecdh[32];
    assert_int_equal(ehem_sim_shared(identity, eid_raw, ecdh), 0);
    char *plain = NULL;
    assert_int_equal(ehem_sim_scheme_a_decrypt(value, jti_raw, ecdh, &plain),
                     0);
    return plain;
}

static void test_full_login_roundtrip(void **state)
{
    (void)state;
    ehem_sim_keypair identity;
    uint8_t eid_raw[32], jti_raw[32];
    char *pid_b64 = NULL;
    char *kid = pair_sim(&identity, eid_raw, &pid_b64);

    /* --- request on a NEVER-LOGGED-IN context: the no-auth probe ---------- */
    ehem_ctx *anon = NULL;
    assert_int_equal(ehem_test_ctx(&anon), EHEM_OK);
    ehem_json *payload = run_request(anon, "system:config", "ehemtest",
                                     "EHEMTEST simulated login", eid_raw,
                                     jti_raw);

    /* Claims: iss = eid, 60-min lifetime, ctx/note echoed. */
    const char *sv = NULL;
    int64_t iat = 0, exp = 0;
    char *eid_b64 = ehem_sim_b64(eid_raw, 32);
    assert_true(ehem_json_get_string(payload, "iss", &sv));
    assert_string_equal(sv, eid_b64);
    assert_true(ehem_json_get_int64(payload, "iat", &iat));
    assert_true(ehem_json_get_int64(payload, "exp", &exp));
    assert_int_equal(exp - iat, 3600);
    assert_true(ehem_json_get_string(payload, "ctx", &sv));
    assert_string_equal(sv, "ehemtest");
    assert_true(ehem_json_get_string(payload, "note", &sv));
    assert_string_equal(sv, "EHEMTEST simulated login");

    /* --- decrypt our entry, countersign, redeem --------------------------- */
    char *plain = decrypt_own_entry(payload, pid_b64, &identity, eid_raw,
                                    jti_raw);
    assert_string_equal(plain, "system:config");

    uint8_t ecdh[32];
    assert_int_equal(ehem_sim_shared(&identity, eid_raw, ecdh), 0);
    char *scope_value = NULL;
    assert_int_equal(ehem_sim_scheme_a_encrypt("system:config", jti_raw, ecdh,
                                               &scope_value), 0);

    char *jti_b64 = ehem_sim_b64(jti_raw, 32);
    char *iss_b64 = ehem_sim_b64(identity.pub, 32);
    ehem_json *reply_claims = ehem_json_new_object();
    assert_non_null(reply_claims);
    assert_true(ehem_json_add_string(reply_claims, "jti", jti_b64));
    assert_true(ehem_json_add_string(reply_claims, "iss", iss_b64));
    assert_true(ehem_json_add_string(reply_claims, "pid", pid_b64));
    assert_true(ehem_json_add_string(reply_claims, "scope", scope_value));
    assert_true(ehem_json_add_string(reply_claims, "ctx", "ehemtest"));
    assert_true(ehem_json_add_int64(reply_claims, "exp", exp));
    char *authreply = NULL;
    assert_int_equal(ehem_sim_jwt_build(reply_claims, ecdh, &authreply), 0);

    /* Tampered reply first: flip one signature character → 401 AUTH_FAILED. */
    {
        size_t n = strlen(authreply);
        char saved = authreply[n - 1];
        authreply[n - 1] = (saved == 'A') ? 'B' : 'A';
        char *tok = NULL;
        assert_int_equal(ehem_ext_token(anon, authreply, &tok),
                         EHEM_ERR_AUTH_FAILED);
        assert_int_equal(ehem_last_error(anon)->http_status, 401);
        assert_null(tok);
        authreply[n - 1] = saved;
    }

    char *token = NULL;
    assert_int_equal(ehem_ext_token(anon, authreply, &token), EHEM_OK);
    assert_non_null(token);

    /* --- bearer claims ----------------------------------------------------- */
    ehem_json *bearer = ehem_sim_jwt_peek(token);
    assert_non_null(bearer);
    uint8_t kid_raw[16];
    for (size_t i = 0; i < sizeof kid_raw; i++) {
        unsigned b;
        assert_int_equal(sscanf(kid + 2 * i, "%2x", &b), 1);
        kid_raw[i] = (uint8_t)b;
    }
    char *sub_expect = ehem_sim_b64(kid_raw, sizeof kid_raw);
    assert_true(ehem_json_get_string(bearer, "sub", &sv));
    assert_string_equal(sv, sub_expect);          /* sub = base64(kid) */
    assert_true(ehem_json_get_string(bearer, "scope", &sv));
    assert_string_equal(sv, "system:config");
    int64_t bearer_exp = 0;
    assert_true(ehem_json_get_int64(bearer, "exp", &bearer_exp));
    assert_int_equal(bearer_exp, exp);            /* exp = authreply's exp */
    assert_true(ehem_json_get_string(bearer, "ctx", &sv));
    assert_string_equal(sv, "ehemtest");
    free(sub_expect);
    ehem_json_free(bearer);

    /* --- the bearer WORKS: authenticated config GET ------------------------ */
    {
        char authz[2048];
        int m = snprintf(authz, sizeof authz, "Bearer %s", token);
        assert_true(m > 0 && (size_t)m < sizeof authz);
        ehem_header hdr = { "Authorization", authz };
        ehem_request req;
        memset(&req, 0, sizeof req);
        req.method = EHEM_HTTP_GET;
        req.path = "/api/system/config";
        req.headers = &hdr;
        req.header_count = 1;
        ehem_response resp;
        memset(&resp, 0, sizeof resp);
        assert_int_equal(ehem_transport_send(g_ctx->transport, &req, &resp),
                         EHEM_OK);
        assert_int_equal(resp.status, 200);
        assert_non_null(resp.body);
        assert_non_null(strstr((const char *)resp.body, "devid"));
        ehem_response_free(&resp);
    }

    /* --- scope rewrite: keymgmt:use:<kid> → #-meta + 15-min lifetime ------- */
    {
        char use_scope[64];
        snprintf(use_scope, sizeof use_scope, "keymgmt:use:%s", kid);
        uint8_t jti2[32];
        ehem_json *p2 = run_request(anon, use_scope, NULL, NULL, eid_raw,
                                    jti2);
        int64_t iat2 = 0, exp2 = 0;
        assert_true(ehem_json_get_int64(p2, "iat", &iat2));
        assert_true(ehem_json_get_int64(p2, "exp", &exp2));
        assert_int_equal(exp2 - iat2, 900);   /* 15 min */

        char *plain2 = decrypt_own_entry(p2, pid_b64, &identity, eid_raw,
                                         jti2);
        /* "keymgmt:use:<kid>#<base64 {"t":...,"l":...}>" */
        assert_memory_equal(plain2, use_scope, strlen(use_scope));
        char *hash = strchr(plain2, '#');
        assert_non_null(hash);
        uint8_t meta[128];
        size_t mn = ehem_b64_std_decode(hash + 1, strlen(hash + 1), meta,
                                        sizeof meta);
        assert_int_not_equal(mn, (size_t)-1);
        ehem_json *meta_doc = ehem_json_parse((const char *)meta, mn);
        assert_non_null(meta_doc);
        assert_true(ehem_json_get_string(meta_doc, "l", &sv));
        assert_string_equal(sv, SIM_LABEL);
        assert_true(ehem_json_has(meta_doc, "t"));
        ehem_json_free(meta_doc);
        free(plain2);
        ehem_json_free(p2);
    }

    ehem_ext_token_free(token);
    free(authreply);
    ehem_json_free(reply_claims);
    free(jti_b64);
    free(iss_b64);
    free(scope_value);
    free(plain);
    free(eid_b64);
    ehem_json_free(payload);
    ehem_ctx_destroy(anon);
    free(pid_b64);
    free(kid);
}

static int setup(void **state)
{
    (void)state;
    memset(&g_reg, 0, sizeof g_reg);
    if (ehem_test_ctx(&g_ctx) != EHEM_OK) {
        return -1;
    }
    if (ehem_login(g_ctx, ehem_test_passphrase()) != EHEM_OK) {
        return -1;
    }
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    ehem_test_cleanup(g_ctx, &g_reg);
    ehem_ctx_destroy(g_ctx);
    g_ctx = NULL;
    return 0;
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return 77;
    }
    ehem_test_reboot_if_requested();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_full_login_roundtrip,
                                        setup, teardown),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
