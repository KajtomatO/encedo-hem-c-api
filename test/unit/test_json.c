/*
 * Unit tests for hem_json.c helpers:
 *   - hem_base64_encode / hem_base64_decode
 *   - hem_json_get_str / hem_json_get_int64 / hem_json_get_bool
 *
 * Run without a device.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cmocka.h>

/* Pull in the internal header so we can call the helpers directly */
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * base64 encode
 * ---------------------------------------------------------------------- */

static void test_b64_encode_empty(void **state)
{
    (void)state;
    char *out = hem_base64_encode((const uint8_t *)"", 0);
    assert_non_null(out);
    assert_string_equal(out, "");
    free(out);
}

static void test_b64_encode_one_byte(void **state)
{
    (void)state;
    /* 0x00 -> "AA==" */
    uint8_t data[] = {0x00};
    char *out = hem_base64_encode(data, 1);
    assert_non_null(out);
    assert_string_equal(out, "AA==");
    free(out);
}

static void test_b64_encode_two_bytes(void **state)
{
    (void)state;
    /* 0x00 0x00 -> "AAA=" */
    uint8_t data[] = {0x00, 0x00};
    char *out = hem_base64_encode(data, 2);
    assert_non_null(out);
    assert_string_equal(out, "AAA=");
    free(out);
}

static void test_b64_encode_three_bytes(void **state)
{
    (void)state;
    /* "Man" (RFC 4648 example) -> "TWFu" */
    char *out = hem_base64_encode((const uint8_t *)"Man", 3);
    assert_non_null(out);
    assert_string_equal(out, "TWFu");
    free(out);
}

static void test_b64_encode_hello_world(void **state)
{
    (void)state;
    char *out = hem_base64_encode((const uint8_t *)"Hello, World!", 13);
    assert_non_null(out);
    assert_string_equal(out, "SGVsbG8sIFdvcmxkIQ==");
    free(out);
}

static void test_b64_encode_all_bytes(void **state)
{
    (void)state;
    uint8_t data[3] = {0xFB, 0xFF, 0xFE};
    char *out = hem_base64_encode(data, 3);
    assert_non_null(out);
    /* 0xFB 0xFF 0xFE: bits 111110 111111 111111 111110 -> "+//+" */
    assert_string_equal(out, "+//+");
    free(out);
}

/* -------------------------------------------------------------------------
 * base64 decode
 * ---------------------------------------------------------------------- */

static void test_b64_decode_empty(void **state)
{
    (void)state;
    size_t len = 99;
    uint8_t *out = hem_base64_decode("", &len);
    /* empty string: length 0 is not a multiple of 4, returns NULL */
    /* actually "" has length 0, which passes the % 4 == 0 check */
    /* implementation: in_len=0, out_len=0, malloc(0) may return non-NULL */
    assert_int_equal((int)len, 0);
    free(out); /* safe even if NULL */
}

static void test_b64_decode_hello_world(void **state)
{
    (void)state;
    size_t len = 0;
    uint8_t *out = hem_base64_decode("SGVsbG8sIFdvcmxkIQ==", &len);
    assert_non_null(out);
    assert_int_equal((int)len, 13);
    assert_memory_equal(out, "Hello, World!", 13);
    free(out);
}

static void test_b64_decode_man(void **state)
{
    (void)state;
    size_t len = 0;
    uint8_t *out = hem_base64_decode("TWFu", &len);
    assert_non_null(out);
    assert_int_equal((int)len, 3);
    assert_memory_equal(out, "Man", 3);
    free(out);
}

static void test_b64_decode_one_pad(void **state)
{
    (void)state;
    size_t len = 0;
    uint8_t *out = hem_base64_decode("AA==", &len);
    assert_non_null(out);
    assert_int_equal((int)len, 1);
    assert_int_equal(out[0], 0x00);
    free(out);
}

static void test_b64_decode_invalid_chars(void **state)
{
    (void)state;
    size_t len = 0;
    uint8_t *out = hem_base64_decode("!!!!",  &len);
    assert_null(out);
}

static void test_b64_roundtrip(void **state)
{
    (void)state;
    uint8_t original[32];
    for (int i = 0; i < 32; i++) original[i] = (uint8_t)(i * 7 + 13);

    char *encoded = hem_base64_encode(original, 32);
    assert_non_null(encoded);

    size_t dec_len = 0;
    uint8_t *decoded = hem_base64_decode(encoded, &dec_len);
    free(encoded);

    assert_non_null(decoded);
    assert_int_equal((int)dec_len, 32);
    assert_memory_equal(decoded, original, 32);
    free(decoded);
}

/* -------------------------------------------------------------------------
 * JSON helpers
 * ---------------------------------------------------------------------- */

static void test_json_get_str_present(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"key\": \"value\"}");
    assert_non_null(obj);

    char buf[64] = {0};
    hem_json_get_str(obj, "key", buf, sizeof(buf));
    assert_string_equal(buf, "value");
    cJSON_Delete(obj);
}

static void test_json_get_str_absent(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"other\": \"x\"}");
    assert_non_null(obj);

    char buf[64] = "sentinel";
    hem_json_get_str(obj, "missing", buf, sizeof(buf));
    assert_string_equal(buf, "");
    cJSON_Delete(obj);
}

static void test_json_get_str_truncates(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"k\": \"abcdefgh\"}");
    assert_non_null(obj);

    char buf[5];
    hem_json_get_str(obj, "k", buf, sizeof(buf));
    assert_string_equal(buf, "abcd");  /* truncated to 4 + NUL */
    cJSON_Delete(obj);
}

static void test_json_get_int64_present(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"n\": 42}");
    assert_non_null(obj);

    int64_t v = hem_json_get_int64(obj, "n", -1);
    assert_int_equal((int)v, 42);
    cJSON_Delete(obj);
}

static void test_json_get_int64_absent(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"other\": 1}");
    assert_non_null(obj);

    int64_t v = hem_json_get_int64(obj, "missing", 99);
    assert_int_equal((int)v, 99);
    cJSON_Delete(obj);
}

static void test_json_get_bool_true(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"flag\": true}");
    assert_non_null(obj);

    bool v = hem_json_get_bool(obj, "flag", false);
    assert_true(v);
    cJSON_Delete(obj);
}

static void test_json_get_bool_false(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"flag\": false}");
    assert_non_null(obj);

    bool v = hem_json_get_bool(obj, "flag", true);
    assert_false(v);
    cJSON_Delete(obj);
}

static void test_json_get_bool_absent(void **state)
{
    (void)state;
    cJSON *obj = cJSON_Parse("{\"other\": 1}");
    assert_non_null(obj);

    bool v = hem_json_get_bool(obj, "missing", true);
    assert_true(v);
    cJSON_Delete(obj);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* base64 encode */
        cmocka_unit_test(test_b64_encode_empty),
        cmocka_unit_test(test_b64_encode_one_byte),
        cmocka_unit_test(test_b64_encode_two_bytes),
        cmocka_unit_test(test_b64_encode_three_bytes),
        cmocka_unit_test(test_b64_encode_hello_world),
        cmocka_unit_test(test_b64_encode_all_bytes),
        /* base64 decode */
        cmocka_unit_test(test_b64_decode_empty),
        cmocka_unit_test(test_b64_decode_hello_world),
        cmocka_unit_test(test_b64_decode_man),
        cmocka_unit_test(test_b64_decode_one_pad),
        cmocka_unit_test(test_b64_decode_invalid_chars),
        cmocka_unit_test(test_b64_roundtrip),
        /* JSON helpers */
        cmocka_unit_test(test_json_get_str_present),
        cmocka_unit_test(test_json_get_str_absent),
        cmocka_unit_test(test_json_get_str_truncates),
        cmocka_unit_test(test_json_get_int64_present),
        cmocka_unit_test(test_json_get_int64_absent),
        cmocka_unit_test(test_json_get_bool_true),
        cmocka_unit_test(test_json_get_bool_false),
        cmocka_unit_test(test_json_get_bool_absent),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
