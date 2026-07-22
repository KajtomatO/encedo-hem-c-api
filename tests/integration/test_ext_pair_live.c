/*
 * test_ext_pair_live.c — live ExtAuth pairing against the real device via the
 * simulated authenticator: init → locally-built reply → validate → local code
 * verification → key inspection → dedup probe → mac → cleanup. No broker, no
 * phone (REQ-TEST-006).
 *
 * verifies: REQ-AUTH-006 (all three pairing bindings live: the request JWT's
 *           signature verifies with ECDH(our ephemeral, eid) proving the
 *           documented construction + the standard-base64 claim convention;
 *           validate imports our key under "EXTAID"+pid with our label and
 *           returns a code equal to the locally-computed
 *           HMAC-SHA256(reply, ECDH(confirm_epk, EIDkey)); re-pairing the
 *           SAME identity key → 406 DEVICE (repo dedup); mac verifies
 *           locally against HMAC-SHA256(nonce, ECDH(ephemeral, EIDkey))),
 *           REQ-TEST-006 (the simulated-authenticator cycle runs unattended
 *           with zero broker traffic; EHEMTEST-labeled, tracked, cleaned)
 *
 * Internal-linking exception (like test_auth_live): the simulated
 * authenticator needs the crypto shim, so this links the static lib via
 * ehem_test_support. Label stays `integration`.
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
#include "crypto_shim.h"      /* internal: HMAC for code/mac verification */
#include "ext_sim.h"          /* simulated authenticator */
#include "json.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

#define SIM_LABEL "EHEMTEST-extsim"

static ehem_ctx *g_ctx;
static ehem_test_keyreg g_reg;

/* One init exchange: fresh ephemeral → ehem_ext_init → verify the request
 * JWT with ECDH(ephemeral, eid) and hand back its jti (raw) + eid (raw). */
static void run_init(const ehem_sim_keypair *eph, ehem_ext_init_info **info,
                     uint8_t jti_raw[32], uint8_t eid_raw[32])
{
    char *epk_b64 = ehem_sim_b64(eph->pub, 32);
    assert_non_null(epk_b64);
    assert_int_equal(ehem_ext_init(g_ctx, epk_b64, info), EHEM_OK);
    assert_non_null(*info);

    /* eid: standard base64 of a 32-byte Curve25519 point. */
    assert_int_equal(ehem_sim_b64_key((*info)->eid, eid_raw), 0);

    /* The request JWT is HS256 over ECDH(EIDkey, our ephemeral): verify with
     * our side of the same secret — proves the documented construction AND
     * the claim base64 convention in one stroke. */
    uint8_t secret[32];
    assert_int_equal(ehem_sim_shared(eph, eid_raw, secret), 0);
    ehem_json *payload = ehem_sim_jwt_open((*info)->request, secret);
    assert_non_null(payload);

    const char *jti_b64 = NULL, *aud = NULL, *iss = NULL;
    assert_true(ehem_json_get_string(payload, "jti", &jti_b64));
    assert_int_equal(ehem_sim_b64_key(jti_b64, jti_raw), 0);
    assert_true(ehem_json_get_string(payload, "aud", &aud));
    assert_string_equal(aud, epk_b64);          /* our epk echoed */
    assert_true(ehem_json_get_string(payload, "iss", &iss));
    assert_string_equal(iss, (*info)->eid);     /* device identity */
    ehem_json_free(payload);
    free(epk_b64);
}

/* Build the authenticator's reply for a request: echo the jti, present the
 * identity pubkey as iss, our label, and the confirmation epk. */
static char *build_reply(const uint8_t jti_raw[32],
                         const uint8_t eid_raw[32],
                         const ehem_sim_keypair *identity,
                         const ehem_sim_keypair *confirm)
{
    char *jti_b64 = ehem_sim_b64(jti_raw, 32);
    char *iss_b64 = ehem_sim_b64(identity->pub, 32);
    char *epk_b64 = ehem_sim_b64(confirm->pub, 32);
    assert_non_null(jti_b64);
    assert_non_null(iss_b64);
    assert_non_null(epk_b64);

    ehem_json *claims = ehem_json_new_object();
    assert_non_null(claims);
    assert_true(ehem_json_add_string(claims, "jti", jti_b64));
    assert_true(ehem_json_add_string(claims, "iss", iss_b64));
    assert_true(ehem_json_add_string(claims, "label", SIM_LABEL));
    assert_true(ehem_json_add_string(claims, "epk", epk_b64));

    uint8_t sign_key[32];
    assert_int_equal(ehem_sim_shared(identity, eid_raw, sign_key), 0);
    char *reply = NULL;
    assert_int_equal(ehem_sim_jwt_build(claims, sign_key, &reply), 0);

    ehem_json_free(claims);
    free(jti_b64);
    free(iss_b64);
    free(epk_b64);
    return reply;
}

static void test_pair_validate_mac_cycle(void **state)
{
    (void)state;
    ehem_sim_keypair eph, identity, confirm, pid_src;
    uint8_t jti_raw[32], eid_raw[32];

    assert_int_equal(ehem_sim_keypair_gen(&eph), 0);
    assert_int_equal(ehem_sim_keypair_gen(&identity), 0);
    assert_int_equal(ehem_sim_keypair_gen(&confirm), 0);
    assert_int_equal(ehem_sim_keypair_gen(&pid_src), 0);  /* 32 unique bytes */

    /* --- init + request JWT verification --------------------------------- */
    ehem_ext_init_info *init = NULL;
    run_init(&eph, &init, jti_raw, eid_raw);

    /* --- validate --------------------------------------------------------- */
    char *reply = build_reply(jti_raw, eid_raw, &identity, &confirm);
    char *pid_b64 = ehem_sim_b64(pid_src.pub, 32);
    assert_non_null(pid_b64);

    ehem_ext_validate_info *val = NULL;
    assert_int_equal(ehem_ext_validate(g_ctx, pid_b64, reply, &val), EHEM_OK);
    assert_non_null(val);
    ehem_test_track(&g_reg, val->kid);

    /* code == HMAC-SHA256(reply-string, ECDH(confirm, EIDkey)) in standard
     * base64 — the open REQ-AUTH-006 criterion on the base64 alphabet. */
    uint8_t code_key[32], code_tag[32];
    assert_int_equal(ehem_sim_shared(&confirm, eid_raw, code_key), 0);
    assert_int_equal(ehem_hmac_sha256(code_key, 32, (const uint8_t *)reply,
                                      strlen(reply), code_tag), EHEM_OK);
    char *code_expect = ehem_sim_b64(code_tag, 32);
    assert_non_null(code_expect);
    assert_string_equal(val->code, code_expect);
    free(code_expect);

    /* --- the imported key is real and carries our material ---------------- */
    ehem_key_details *det = NULL;
    assert_int_equal(ehem_key_get(g_ctx, val->kid, &det), EHEM_OK);
    assert_non_null(det);
    assert_non_null(det->pubkey);
    assert_int_equal(det->pubkey_len, 32);
    assert_memory_equal(det->pubkey, identity.pub, 32);
    /* descriptor = "EXTAID" + the decoded pid */
    assert_int_equal(det->descr_len, 6 + 32);
    assert_memory_equal(det->descr, "EXTAID", 6);
    assert_memory_equal(det->descr + 6, pid_src.pub, 32);
    ehem_key_details_free(det);

    /* --- dedup probe: same identity key again → 406 ----------------------- */
    ehem_ext_init_info *init2 = NULL;
    uint8_t jti2[32], eid2[32];
    ehem_sim_keypair eph2, pid2;
    assert_int_equal(ehem_sim_keypair_gen(&eph2), 0);
    assert_int_equal(ehem_sim_keypair_gen(&pid2), 0);
    run_init(&eph2, &init2, jti2, eid2);
    assert_memory_equal(eid_raw, eid2, 32);   /* stable device identity */

    char *reply2 = build_reply(jti2, eid2, &identity, &confirm);
    char *pid2_b64 = ehem_sim_b64(pid2.pub, 32);
    ehem_ext_validate_info *val2 = NULL;
    ehem_rc rc = ehem_ext_validate(g_ctx, pid2_b64, reply2, &val2);
    assert_int_equal(rc, EHEM_ERR_DEVICE);
    assert_null(val2);
    assert_int_equal(ehem_last_error(g_ctx)->http_status, 406);

    /* --- mac: liveness proof verified locally ----------------------------- */
    ehem_sim_keypair mac_eph;
    assert_int_equal(ehem_sim_keypair_gen(&mac_eph), 0);
    char *mac_epk_b64 = ehem_sim_b64(mac_eph.pub, 32);
    ehem_ext_mac_info *mac = NULL;
    assert_int_equal(ehem_ext_mac(g_ctx, mac_epk_b64, &mac), EHEM_OK);
    assert_non_null(mac);
    assert_string_equal(mac->eid, init->eid);

    uint8_t nonce_raw[32], mac_key[32], mac_tag[32];
    assert_int_equal(ehem_sim_b64_key(mac->nonce, nonce_raw), 0);
    assert_int_equal(ehem_sim_shared(&mac_eph, eid_raw, mac_key), 0);
    assert_int_equal(ehem_hmac_sha256(mac_key, 32, nonce_raw, 32, mac_tag),
                     EHEM_OK);
    char *mac_expect = ehem_sim_b64(mac_tag, 32);
    assert_non_null(mac_expect);
    assert_string_equal(mac->mac, mac_expect);
    free(mac_expect);

    ehem_ext_mac_free(mac);
    free(mac_epk_b64);
    free(reply2);
    free(pid2_b64);
    ehem_ext_init_free(init2);
    ehem_ext_validate_free(val);
    free(reply);
    free(pid_b64);
    ehem_ext_init_free(init);
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
        cmocka_unit_test_setup_teardown(test_pair_validate_mac_cycle,
                                        setup, teardown),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
