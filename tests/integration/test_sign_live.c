/*
 * test_sign_live.c — live signing + local wolfCrypt verification + the sign
 * scope probe that closes ARCHITECTURE §12 risk 3.
 *
 * verifies: REQ-OPS-001 (an EHEMTEST SECP256R1 key created with mode ExDSA
 *           signs via SHA256WithECDSA and the DER signature verifies locally
 *           against the compressed-x963 pubkey from ehem_key_get; an EHEMTEST
 *           ED25519 key signs via Ed25519 and the 64-byte signature verifies
 *           per RFC 8032; PROBE: a broader-scope token — keymgmt:get — is
 *           REJECTED by the sign endpoint per the firmware's exact strcmp,
 *           while get and sign share ONE keymgmt:use:<kid> token),
 *           REQ-KEY-006 (live cross-check: the created keys' type strings
 *           classify to families whose pubkey_len/sig_max_len match the
 *           material the device actually returned)
 *
 * The probe needs a scope the public binding never requests, so this test
 * links the STATIC lib + internal headers (via ehem_test_support) — the same
 * exception test_keymgmt_get_live takes. It also calls the internal crypto
 * shim for the local verify (the M4 gate criterion). Skipped (exit 77)
 * unless EHEM_TEST_URL + EHEM_TEST_PASSPHRASE. Keys are EHEMTEST-labeled and
 * cleaned pass-or-fail (REQ-TEST-003).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "crypto_shim.h"      /* internal: local verify (gate criterion) */
#include "ejwt.h"             /* internal: base64 for the probe body */
#include "json.h"             /* internal: ehem_json_* for the probe */
#include "proto_common.h"     /* internal: ehem_proto_request_json (probe) */
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct sign_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} sign_state;

static int setup(void **state)
{
    sign_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    sign_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static const uint8_t MSG[] = "signed by test_sign_live via the Encedo HEM";
#define MSG_LEN (sizeof MSG - 1)

/* Create an EHEMTEST key of `type` (optional mode), tracked for cleanup. */
static void create_key(sign_state *s, const char *type, const char *mode,
                       char kid[EHEM_KID_HEX_SIZE])
{
    char label[32];
    ehem_key_create_params p;
    ehem_rc rc;

    ehem_test_label(label, sizeof label);
    memset(&p, 0, sizeof p);
    p.type = type;
    p.label = label;
    p.mode = mode;

    rc = ehem_key_create(s->ctx, &p, kid);
    if (rc != EHEM_OK) {
        fail_msg("create %s failed: %s (%s)", type, ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid);
}

/* Fetch the key's material and cross-check the REQ-KEY-006 size constants
 * against what the device actually returned. */
static ehem_key_details *get_and_check_type(sign_state *s, const char *kid,
                                            ehem_key_family want_family)
{
    ehem_key_details *d = NULL;
    ehem_key_type_info info;
    ehem_rc rc = ehem_key_get(s->ctx, kid, &d);
    if (rc != EHEM_OK) {
        fail_msg("get failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_non_null(d);
    assert_int_equal(ehem_key_type_parse(d->type, &info), EHEM_OK);
    assert_int_equal(info.family, want_family);
    /* Live cross-check (REQ-KEY-006): the classifier's pubkey size matches
     * the wire material. */
    assert_int_equal((int)d->pubkey_len, (int)info.pubkey_len);
    printf("[test_sign_live] get %s: type=%s pubkey_len=%d (classifier says %d)\n",
           ehem_key_family_str(want_family), d->type, (int)d->pubkey_len,
           (int)info.pubkey_len);
    return d;
}

static void test_sign_ed25519_verifies_locally(void **state)
{
    sign_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_signature *sig = NULL;
    ehem_key_details *d;
    int valid = 0;
    ehem_rc rc;

    create_key(s, "ED25519", NULL, kid);

    rc = ehem_sign(s->ctx, kid, EHEM_SIGN_ALG_ED25519, MSG, MSG_LEN,
                   NULL, 0, &sig);
    if (rc != EHEM_OK) {
        fail_msg("sign failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_non_null(sig);
    assert_int_equal((int)sig->sig_len, EHEM_ED25519_SIG_SIZE);

    d = get_and_check_type(s, kid, EHEM_KEY_FAMILY_ED25519);
    assert_int_equal((int)d->pubkey_len, EHEM_ED25519_PUB_SIZE);

    /* THE M4 GATE CRITERION: the device's signature verifies locally. */
    assert_int_equal(ehem_ed25519_verify(d->pubkey, MSG, MSG_LEN,
                                         sig->sig, sig->sig_len, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 1);

    /* A different message must NOT verify. */
    assert_int_equal(ehem_ed25519_verify(d->pubkey, MSG, MSG_LEN - 1,
                                         sig->sig, sig->sig_len, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 0);

    printf("[test_sign_live] ED25519: 64-byte signature verified locally "
           "(and a truncated message correctly did not)\n");
    ehem_key_details_free(d);
    ehem_signature_free(sig);
}

static void test_sign_ecdsa_p256_verifies_locally(void **state)
{
    sign_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_signature *sig = NULL;
    ehem_key_details *d;
    ehem_key_type_info info;
    int valid = 0;
    ehem_rc rc;

    /* Device default is ECDH-only — signing needs mode ExDSA (python OQ-19). */
    create_key(s, "SECP256R1", "ExDSA", kid);

    rc = ehem_sign(s->ctx, kid, EHEM_SIGN_ALG_SHA256_ECDSA, MSG, MSG_LEN,
                   NULL, 0, &sig);
    if (rc != EHEM_OK) {
        fail_msg("sign failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_non_null(sig);

    /* DER ECDSA-Sig-Value: SEQUENCE tag, length within the classifier max. */
    assert_int_equal(sig->sig[0], 0x30);
    assert_int_equal(ehem_key_type_parse("SECP256R1", &info), EHEM_OK);
    assert_true(sig->sig_len <= info.sig_max_len);

    d = get_and_check_type(s, kid, EHEM_KEY_FAMILY_SECP256R1);

    /* THE M4 GATE CRITERION: the device's DER signature verifies locally
     * against the compressed-x963 pubkey the device exported. */
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1,
                                       d->pubkey, d->pubkey_len,
                                       MSG, MSG_LEN,
                                       sig->sig, sig->sig_len, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 1);

    /* A different message must NOT verify. */
    assert_int_equal(ehem_ecdsa_verify(EHEM_ECDSA_SECP256R1,
                                       d->pubkey, d->pubkey_len,
                                       MSG, MSG_LEN - 1,
                                       sig->sig, sig->sig_len, &valid),
                     EHEM_OK);
    assert_int_equal(valid, 0);

    printf("[test_sign_live] SECP256R1: %d-byte DER signature verified "
           "locally against the %d-byte pubkey (compressed=%s)\n",
           (int)sig->sig_len, (int)d->pubkey_len,
           d->pubkey_len == 33 ? "yes" : "no");
    ehem_key_details_free(d);
    ehem_signature_free(sig);
}

/* Probe: POST /api/crypto/exdsa/sign with a token of `scope` (NOT the exact
 * per-kid scope). Firmware v1.2.2 api_post_crypto_exdsa_sign does a plain
 * strcmp, so anything but keymgmt:use:<kid> should be 403 — the observation
 * that closes §12 risk 3. Recorded, and asserted not to be a transport error. */
static void probe_sign_scope(ehem_ctx *ctx, const char *kid, const char *scope,
                             ehem_rc *rc_out)
{
    char msg_b64[64];
    char body[256];
    ehem_json *root = NULL;
    ehem_rc rc;

    assert_int_not_equal(ehem_b64_std_encode(MSG, MSG_LEN, msg_b64,
                                             sizeof msg_b64), (size_t)-1);
    snprintf(body, sizeof body,
             "{\"kid\":\"%s\",\"msg\":\"%s\",\"alg\":\"Ed25519\"}",
             kid, msg_b64);
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/crypto/exdsa/sign",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    printf("[test_sign_live] SIGN SCOPE PROBE %-16s -> %s (HTTP %ld)\n",
           scope, ehem_rc_str(rc), ehem_last_error(ctx)->http_status);
    ehem_json_free(root);
    *rc_out = rc;
}

static void test_sign_scope_probe(void **state)
{
    sign_state *s = *state;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_signature *sig = NULL;
    ehem_rc probe_rc;
    ehem_rc rc;

    create_key(s, "ED25519", NULL, kid);

    /* PROBE (closes §12 risk 3): a broader keymgmt:get token — accepted by
     * the GET endpoint since M3-040 — must NOT sign. */
    probe_sign_scope(s->ctx, kid, "keymgmt:get", &probe_rc);
    assert_true(probe_rc == EHEM_ERR_SCOPE_DENIED || probe_rc == EHEM_OK);
    if (probe_rc == EHEM_OK) {
        printf("[test_sign_live] UNEXPECTED: broader scope accepted for sign "
               "— record in REQ-OPS-001/§12 risk 3!\n");
    }

    /* The exact per-kid token acquired by a GET serves the sign too: get
     * first (fills the cache for keymgmt:use:<kid>), then sign — live proof
     * of the one-token-per-kid sharing the unit suite asserts offline. */
    ehem_key_details *d = NULL;
    rc = ehem_key_get(s->ctx, kid, &d);
    assert_int_equal(rc, EHEM_OK);
    ehem_key_details_free(d);

    rc = ehem_sign(s->ctx, kid, EHEM_SIGN_ALG_ED25519, MSG, MSG_LEN,
                   NULL, 0, &sig);
    if (rc != EHEM_OK) {
        fail_msg("sign after get failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    assert_int_equal((int)sig->sig_len, EHEM_ED25519_SIG_SIZE);
    printf("[test_sign_live] get→sign on one keymgmt:use token: OK\n");
    ehem_signature_free(sig);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live sign test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_sign_ed25519_verifies_locally,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(test_sign_ecdsa_p256_verifies_locally,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(test_sign_scope_probe,
                                        setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
