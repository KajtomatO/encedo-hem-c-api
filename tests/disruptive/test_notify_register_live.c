/*
 * test_notify_register_live.c — live probe of the broker's registration
 * flow up to the phone leg: session → device ext/init → register/init →
 * two register/check polls (expected pending). `disruptive`-labeled
 * (REQ-TEST-006): it creates a DANGLING broker registration — the rid is
 * never scanned or finalised and expires broker-side. Public API only.
 *
 * verifies: REQ-AUTH-008 (register/init accepts {epk, eid, request} and
 *           returns {rid, link}; register/check answers 202/pending while
 *           the phone leg is outstanding)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <ehem/ehem.h>
#include <ehem/auth.h>
#include "integration_env.h"

static void test_register_init_and_pending(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    /* The broker binds a registration to a session epk obtained via the
     * POST-with-eid form (probed 2026-07-23: a GET-form epk → 401 at
     * register/init). Bootstrap the eid credential-free: any authreq names
     * it as iss — but that needs ext_sim (internal); instead take it from
     * ext/init with a throwaway GET-form epk, which is authed and public. */
    char *boot_epk = NULL;
    ehem_ext_init_info *boot = NULL;
    assert_int_equal(ehem_notify_session(ctx, NULL, NULL, &boot_epk), EHEM_OK);
    assert_int_equal(ehem_ext_init(ctx, boot_epk, &boot), EHEM_OK);

    /* The real pairing sequence: session POST {eid} → epk → ext/init(epk). */
    char *epk = NULL;
    assert_int_equal(ehem_notify_session(ctx, NULL, boot->eid, &epk), EHEM_OK);
    ehem_ext_init_info *init = NULL;
    assert_int_equal(ehem_ext_init(ctx, epk, &init), EHEM_OK);

    ehem_notify_register_info *reg = NULL;
    ehem_rc rc = ehem_notify_register_init(ctx, NULL, epk, init->eid,
                                           init->request, &reg);
    if (rc != EHEM_OK) {
        fprintf(stderr, "[probe] register/init rc=%d http=%ld payload=%s\n",
                rc, ehem_last_error(ctx)->http_status,
                ehem_last_error(ctx)->device_payload
                    ? ehem_last_error(ctx)->device_payload : "(none)");
    }
    assert_int_equal(rc, EHEM_OK);
    assert_non_null(reg);
    assert_true(strlen(reg->rid) > 0);
    assert_true(strlen(reg->link) > 0);
    fprintf(stderr, "[probe] rid=%s link=%s (left dangling, expires "
                    "broker-side)\n", reg->rid, reg->link);

    /* No phone will scan: both polls must report pending (202). */
    for (int i = 0; i < 2; i++) {
        ehem_notify_pairing_reply *r = NULL;
        assert_int_equal(ehem_notify_register_check(ctx, NULL, reg->rid, &r),
                         EHEM_OK);
        assert_true(r->pending);
        ehem_notify_pairing_reply_free(r);
    }

    ehem_notify_register_info_free(reg);
    ehem_ext_init_free(init);
    ehem_ext_init_free(boot);
    ehem_notify_string_free(epk);
    ehem_notify_string_free(boot_epk);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_disruptive();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return 77;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_register_init_and_pending),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
