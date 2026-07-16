/*
 * test_checkin_live.c — live check-in round-trip: device challenge → Encedo
 * cloud verify → device apply, against the real dev-machine HEM.
 *
 * verifies: REQ-SYS-003 (live), REQ-TEST-002 (gating)
 *
 * Needs both the device (EHEM_TEST_URL) and https://api.encedo.com reachable.
 * Works whether the device certificate is currently valid or expired (the
 * device legs run relaxed by design), so it stays green after the first
 * successful run renews the cert.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "integration_env.h"

static void test_checkin_roundtrip(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_checkin_info *res = NULL;
    ehem_rc rc;

    rc = ehem_test_ctx(&ctx);
    assert_int_equal(rc, EHEM_OK);

    rc = ehem_system_checkin(ctx, &res);
    if (rc != EHEM_OK) {
        fail_msg("ehem_system_checkin failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(res);
    /* Fields are firmware-dependent; the round-trip completing is the test.
     * Print what came back so the run log documents the live shape. */
    print_message("checkin: status=%s newcrt=%s newfws=%s newuis=%s\n",
                  res->status ? res->status : "(absent)",
                  res->newcrt ? res->newcrt : "(absent)",
                  res->newfws ? res->newfws : "(absent)",
                  res->newuis ? res->newuis : "(absent)");

    /* REQ-SYS-006 (live): the cloud delivers a chain; its leaf is CN=my.ence.do
     * and its serial is what skip-if-current compares against the device's
     * current serial (leg-1 csn). Both are optional on the wire (firmware/broker
     * variants), so assert only when present — and print them for the run log. */
    print_message("harvest: current_serial=%s newcrt_chain=%s\n",
                  res->current_serial ? res->current_serial : "(absent)",
                  res->newcrt_chain ? "(present)" : "(absent)");
    if (res->newcrt_chain != NULL) {
        ehem_cert_info *leaf = NULL;
        rc = ehem_cert_inspect(ctx, res->newcrt_chain, &leaf);
        assert_int_equal(rc, EHEM_OK);
        assert_non_null(leaf);
        print_message("harvest: leaf CN=%s serial=%s valid %s..%s\n",
                      leaf->subject_cn, leaf->serial,
                      leaf->not_before, leaf->not_after);
        assert_string_equal(leaf->subject_cn, "my.ence.do");
        assert_true(leaf->serial[0] != '\0');
        ehem_cert_info_free(leaf);
    }

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_test_url();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checkin_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
