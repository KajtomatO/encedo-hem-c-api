/*
 * test_notify_session_live.c — live probe of the broker's session endpoint,
 * both forms (REQ-AUTH-008): the credential-free GET the login flow uses and
 * the POST-with-eid the pairing flow uses. Read-only broker traffic — the
 * one notify endpoint allowed under the plain `integration` label
 * (REQ-TEST-006); register/event legs are `disruptive`.
 *
 * verifies: REQ-AUTH-008 (session GET → {epk}, session POST {eid} → {epk},
 *           both epks standard base64 of 32 bytes; the eid itself obtained
 *           with ZERO credentials via an /ext/request authreq's iss claim)
 *
 * Internal-linking exception: ehem_sim_jwt_peek reads the authreq payload.
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
#include "ext_sim.h"
#include "json.h"
#include "integration_env.h"

static void test_session_both_forms(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);   /* never logged in */

    /* GET form (login flow, no eid). */
    char *epk = NULL;
    assert_int_equal(ehem_notify_session(ctx, NULL, NULL, &epk), EHEM_OK);
    uint8_t raw[32];
    assert_int_equal(ehem_sim_b64_key(epk, raw), 0);   /* b64 of 32 bytes */

    /* The device's eid, credential-free: any authreq names it as iss. */
    ehem_sim_keypair eph;
    assert_int_equal(ehem_sim_keypair_gen(&eph), 0);
    char *eph_b64 = ehem_sim_b64(eph.pub, 32);
    ehem_ext_request_info *reqi = NULL;
    assert_int_equal(ehem_ext_request(ctx, eph_b64, "keymgmt:list", NULL,
                                      NULL, &reqi), EHEM_OK);
    ehem_json *payload = ehem_sim_jwt_peek(reqi->authreq);
    assert_non_null(payload);
    const char *eid = NULL;
    assert_true(ehem_json_get_string(payload, "iss", &eid));

    /* POST-with-eid form (pairing flow). */
    char *epk2 = NULL;
    assert_int_equal(ehem_notify_session(ctx, NULL, eid, &epk2), EHEM_OK);
    assert_int_equal(ehem_sim_b64_key(epk2, raw), 0);

    ehem_notify_string_free(epk);
    ehem_notify_string_free(epk2);
    ehem_json_free(payload);
    ehem_ext_request_free(reqi);
    free(eph_b64);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_test_url();
    ehem_test_reboot_if_requested();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_both_forms),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
