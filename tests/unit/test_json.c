/*
 * test_json.c — internal JSON helper layer (src/json.h).
 *
 * verifies: REQ-BUILD-003 (a document round-trips through the helper layer;
 *           tolerant getters honor the present/absent/wrong-type contract)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "json.h"

/* A document exercising every getter type plus a nested object, mirroring the
 * shape of a system/status response (numbers, strings, bools, sub-object). */
static const char DOC[] =
    "{"
    "  \"ctx\": \"hem-01\","
    "  \"uptime\": 123456789,"
    "  \"temp\": 41.5,"
    "  \"inited\": true,"
    "  \"https\": false,"
    "  \"storage\": { \"free\": 8388608, \"total\": 16777216 },"
    "  \"nullish\": null,"
    "  \"ignored_unknown\": [1, 2, 3]"
    "}";

/* Parse → read every field via the typed getters → serialize → re-parse and
 * confirm the values survived the round trip. */
static void test_roundtrip(void **state)
{
    (void)state;

    ehem_json *root = ehem_json_parse(DOC, strlen(DOC));
    assert_non_null(root);
    assert_true(ehem_json_is_object(root));

    const char *ctx = NULL;
    int64_t uptime = 0;
    double temp = 0.0;
    bool inited = false, https = true;

    assert_true(ehem_json_get_string(root, "ctx", &ctx));
    assert_string_equal(ctx, "hem-01");
    assert_true(ehem_json_get_int64(root, "uptime", &uptime));
    assert_int_equal(uptime, 123456789);
    assert_true(ehem_json_get_double(root, "temp", &temp));
    assert_true(temp > 41.4 && temp < 41.6);
    assert_true(ehem_json_get_bool(root, "inited", &inited));
    assert_true(inited);
    assert_true(ehem_json_get_bool(root, "https", &https));
    assert_false(https);

    /* Nested object navigation. */
    const ehem_json *storage = ehem_json_get(root, "storage");
    assert_non_null(storage);
    assert_true(ehem_json_is_object(storage));
    int64_t freeb = 0;
    assert_true(ehem_json_get_int64(storage, "free", &freeb));
    assert_int_equal(freeb, 8388608);

    /* Serialize and re-parse: the values must be preserved. */
    char *text = ehem_json_print(root);
    assert_non_null(text);
    ehem_json *root2 = ehem_json_parse(text, strlen(text));
    assert_non_null(root2);
    int64_t uptime2 = 0;
    assert_true(ehem_json_get_int64(root2, "uptime", &uptime2));
    assert_int_equal(uptime2, uptime);
    const char *ctx2 = NULL;
    assert_true(ehem_json_get_string(root2, "ctx", &ctx2));
    assert_string_equal(ctx2, "hem-01");

    ehem_json_string_free(text);
    ehem_json_free(root2);
    ehem_json_free(root);
}

/* The getters treat absent, wrong-type, and JSON-null uniformly: false, with
 * the caller's output left untouched (ARCHITECTURE.md §6 tolerant parsing). */
static void test_absent_and_wrongtype(void **state)
{
    (void)state;

    ehem_json *root = ehem_json_parse(DOC, strlen(DOC));
    assert_non_null(root);

    /* has() distinguishes present (even null) from absent. */
    assert_true(ehem_json_has(root, "ctx"));
    assert_true(ehem_json_has(root, "nullish"));
    assert_false(ehem_json_has(root, "does_not_exist"));

    /* Absent key → false, sentinel untouched. */
    const char *s = "SENTINEL";
    assert_false(ehem_json_get_string(root, "does_not_exist", &s));
    assert_string_equal(s, "SENTINEL");

    /* Present but wrong type: uptime is a number, not a string/bool. */
    int64_t n = 999;
    assert_false(ehem_json_get_int64(root, "ctx", &n));   /* string, not number */
    assert_int_equal(n, 999);
    bool b = true;
    assert_false(ehem_json_get_bool(root, "uptime", &b)); /* number, not bool */
    assert_true(b);

    /* Present-but-null is treated as "no usable value". */
    const char *ns = "KEEP";
    assert_false(ehem_json_get_string(root, "nullish", &ns));
    assert_string_equal(ns, "KEEP");

    /* NULL-safety: getters on NULL object simply fail. */
    assert_false(ehem_json_get_string(NULL, "ctx", &s));
    assert_null(ehem_json_get(NULL, "ctx"));
    assert_false(ehem_json_is_object(NULL));

    ehem_json_free(root);
}

/* Malformed input yields NULL rather than a bogus tree. */
static void test_parse_failure(void **state)
{
    (void)state;
    assert_null(ehem_json_parse("{ not valid json", 16));
    assert_null(ehem_json_parse(NULL, 0));
    /* NULL-safe frees. */
    ehem_json_free(NULL);
    ehem_json_string_free(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_roundtrip),
        cmocka_unit_test(test_absent_and_wrongtype),
        cmocka_unit_test(test_parse_failure),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
