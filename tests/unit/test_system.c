/*
 * test_system.c — system/status and system/version bindings, driven entirely
 * offline with canned doc-example JSON through the fake transport.
 *
 * verifies: REQ-SYS-001, REQ-SYS-002, REQ-API-005
 *   - typed structs populated from documented fields;
 *   - optional fields marked present/absent; unknown fields ignored;
 *   - missing required field → EHEM_ERR_PROTOCOL with detail;
 *   - non-2xx and transport failures mapped per REQ-API-003 with last-error;
 *   - matching *_free() are NULL-safe and leak-clean (ASan/LSan).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "fake_transport.h"

/* The system/status.md example response, plus an unknown field to prove
 * tolerant parsing ignores it. */
static const char STATUS_FULL[] =
    "{"
    "  \"ctx\": 1,"
    "  \"fls_state\": 0,"
    "  \"uptime\": 3600,"
    "  \"ts\": \"2024-01-15T10:30:00Z\","
    "  \"time\": 1705312200,"
    "  \"temp\": 35,"
    "  \"storage\": [\"disk0_status\", \"disk1_status\"],"
    "  \"format\": \"formatting_state\","
    "  \"fw_upgrade\": true,"
    "  \"inited\": true,"
    "  \"https\": true,"
    "  \"hostname\": \"my.ence.do\","
    "  \"repo_stats\": {\"deleted\":0,\"fragmentation\":0,\"freespace\":1024,\"invalid\":0,\"total\":100},"
    "  \"tts\": true,"
    "  \"unknown_future_field\": [1,2,3]"
    "}";

/* Only the required fields (+ storage); every optional omitted. */
static const char STATUS_MINIMAL[] =
    "{ \"ctx\": 2, \"fls_state\": 0, \"uptime\": 42, \"temp\": 40,"
    "  \"storage\": [] }";

/* The system/version.md example response. */
static const char VERSION_FULL[] =
    "{"
    "  \"hwv\": \"PPA rev 2.2\","
    "  \"fwv\": \"Encedo nGINE FW v1.2.2\","
    "  \"fwk\": \"BASE64FWK\","
    "  \"fws\": \"BASE64FWS\","
    "  \"blv\": \"Encedo Secure Bootloader v2.0.1\","
    "  \"blk\": \"BASE64BLK\","
    "  \"bls\": \"BASE64BLS\","
    "  \"uis\": \"managerhash\""
    "}";

/* Build a context whose transport is a fake scripted with one response. The
 * fake is returned via *fake_out; free it after destroying the context. */
static ehem_ctx *ctx_scripted(ehem_transport **fake_out, ehem_rc rc,
                              long status, const char *body)
{
    ehem_transport *fake = fake_transport_new();
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    assert_non_null(fake);
    assert_int_equal(fake_transport_push_response(fake, rc, status, body), 0);
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    *fake_out = fake;
    return ctx;
}

/* --- status --------------------------------------------------------------- */

static void test_status_full(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200, STATUS_FULL);
    ehem_status_info *s = NULL;

    assert_int_equal(ehem_system_status(ctx, &s), EHEM_OK);
    assert_non_null(s);

    /* Required. */
    assert_int_equal(s->ctx, 1);
    assert_int_equal(s->fls_state, 0);
    assert_int_equal(s->uptime, 3600);
    assert_true(s->temp > 34.9 && s->temp < 35.1);

    /* storage array. */
    assert_int_equal((int)s->storage_count, 2);
    assert_string_equal(s->storage[0], "disk0_status");
    assert_string_equal(s->storage[1], "disk1_status");

    /* Optional strings present. */
    assert_string_equal(s->ts, "2024-01-15T10:30:00Z");
    assert_string_equal(s->hostname, "my.ence.do");
    assert_string_equal(s->format, "formatting_state");

    /* Optional number + booleans, with present flags. */
    assert_true(s->has_time);
    assert_int_equal(s->time, 1705312200);
    assert_true(s->has_fw_upgrade && s->fw_upgrade);
    assert_true(s->has_inited && s->inited);
    assert_true(s->has_https && s->https);
    assert_true(s->has_tts && s->tts);

    /* The fixture's repo_stats object is tolerantly IGNORED like any other
     * unknown field — status carries no repo_stats on real firmware
     * (fw emits it only from selftest; surface removed at STEP-M9-015). */

    /* A successful call clears last-error. */
    assert_int_equal(ehem_last_error(ctx)->http_status, 0);
    assert_string_equal(ehem_last_error(ctx)->message, "");

    ehem_system_status_free(s);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_status_minimal_optionals_absent(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200, STATUS_MINIMAL);
    ehem_status_info *s = NULL;

    assert_int_equal(ehem_system_status(ctx, &s), EHEM_OK);
    assert_non_null(s);
    assert_int_equal(s->uptime, 42);
    assert_int_equal((int)s->storage_count, 0);

    /* Optionals absent: strings NULL, has_* false. */
    assert_null(s->ts);
    assert_null(s->hostname);
    assert_null(s->format);
    assert_false(s->has_time);
    assert_false(s->has_inited);
    assert_false(s->has_https);

    ehem_system_status_free(s);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_status_missing_required(void **state)
{
    (void)state;
    ehem_transport *fake;
    /* uptime omitted → required-field violation. */
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200,
        "{ \"ctx\": 1, \"fls_state\": 0, \"temp\": 30 }");
    ehem_status_info *s = (ehem_status_info *)0x1;

    assert_int_equal(ehem_system_status(ctx, &s), EHEM_ERR_PROTOCOL);
    assert_null(s);   /* out reset to NULL on failure */
    assert_non_null(strstr(ehem_last_error(ctx)->message, "uptime"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_status_http_error(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 500,
        "{\"error\":\"internal\"}");
    ehem_status_info *s = NULL;

    assert_int_equal(ehem_system_status(ctx, &s), EHEM_ERR_DEVICE);
    assert_null(s);
    const ehem_error *e = ehem_last_error(ctx);
    assert_int_equal(e->http_status, 500);
    assert_non_null(e->device_payload);
    assert_string_equal(e->device_payload, "{\"error\":\"internal\"}");

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_status_transport_error(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_ERR_UNREACHABLE, 0, NULL);
    ehem_status_info *s = NULL;

    fake_transport_set_detail(fake, "Could not resolve host: hem.local");
    assert_int_equal(ehem_system_status(ctx, &s), EHEM_ERR_UNREACHABLE);
    assert_null(s);
    /* Transport detail is surfaced in the message (REQ-API-004). */
    assert_non_null(strstr(ehem_last_error(ctx)->message, "Could not resolve host"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_status_malformed_json(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200, "this is not json");
    ehem_status_info *s = NULL;

    assert_int_equal(ehem_system_status(ctx, &s), EHEM_ERR_PROTOCOL);
    assert_null(s);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- version -------------------------------------------------------------- */

static void test_version_full(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200, VERSION_FULL);
    ehem_version_info *v = NULL;

    assert_int_equal(ehem_system_version(ctx, &v), EHEM_OK);
    assert_non_null(v);
    assert_string_equal(v->hwv, "PPA rev 2.2");
    assert_string_equal(v->fwv, "Encedo nGINE FW v1.2.2");
    assert_string_equal(v->blv, "Encedo Secure Bootloader v2.0.1");
    assert_string_equal(v->fwk, "BASE64FWK");
    assert_string_equal(v->uis, "managerhash");
    /* Optional microSD fields absent (no token). */
    assert_null(v->sd_csd);
    assert_null(v->sd_cid);

    ehem_system_version_free(v);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* verifies: REQ-SYS-002 — blv/blk/bls are CONDITIONAL (fw emits the triple
 * only with a publisher-matched bootloader footer); a blv-less response is
 * legal and parses with the triple NULL (STEP-M9-015). */
static void test_version_no_bootloader_footer(void **state)
{
    (void)state;
    ehem_transport *fake;
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200,
        "{ \"hwv\": \"PPA rev 2.2\", \"fwv\": \"FW\", \"fwk\": \"K\" }");
    ehem_version_info *v = NULL;

    assert_int_equal(ehem_system_version(ctx, &v), EHEM_OK);
    assert_non_null(v);
    assert_string_equal(v->hwv, "PPA rev 2.2");
    assert_string_equal(v->fwv, "FW");
    assert_null(v->blv);
    assert_null(v->blk);
    assert_null(v->bls);

    ehem_system_version_free(v);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_version_missing_required(void **state)
{
    (void)state;
    ehem_transport *fake;
    /* hwv omitted. */
    ehem_ctx *ctx = ctx_scripted(&fake, EHEM_OK, 200,
        "{ \"fwv\": \"FW\" }");
    ehem_version_info *v = NULL;

    assert_int_equal(ehem_system_version(ctx, &v), EHEM_ERR_PROTOCOL);
    assert_null(v);
    assert_non_null(strstr(ehem_last_error(ctx)->message, "hwv"));

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_free_null_safe(void **state)
{
    (void)state;
    ehem_system_status_free(NULL);
    ehem_system_version_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_status_full),
        cmocka_unit_test(test_status_minimal_optionals_absent),
        cmocka_unit_test(test_status_missing_required),
        cmocka_unit_test(test_status_http_error),
        cmocka_unit_test(test_status_transport_error),
        cmocka_unit_test(test_status_malformed_json),
        cmocka_unit_test(test_version_full),
        cmocka_unit_test(test_version_no_bootloader_footer),
        cmocka_unit_test(test_version_missing_required),
        cmocka_unit_test(test_free_null_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
