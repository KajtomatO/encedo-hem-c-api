/*
 * json.c — tolerant-JSON helper layer over vendored cJSON.
 *
 * implements: REQ-BUILD-003
 *
 * This is the ONLY translation unit that includes cJSON. Everything above it
 * (protocol bindings) speaks the ehem_json / getter vocabulary declared in
 * json.h, so cJSON's type and symbol footprint never escapes this file.
 */
#include "json.h"

#include <limits.h>

#include "vendor/cjson/cJSON.h"

/* ehem_json is a typedef of `struct cJSON`, so the pointer types are identical
 * and no casting is needed between the two vocabularies. */

ehem_json *ehem_json_parse(const char *data, size_t len)
{
    if (data == NULL) {
        return NULL;
    }
    /* Length-delimited parse: transport bodies are not guaranteed NUL-terminated.
     * require_null_terminated = 0 so trailing bytes past the value are tolerated
     * as long as the value itself is well-formed. */
    return cJSON_ParseWithLengthOpts(data, len, NULL, 0);
}

void ehem_json_free(ehem_json *root)
{
    cJSON_Delete(root); /* NULL-safe in cJSON */
}

char *ehem_json_print(const ehem_json *node)
{
    return cJSON_PrintUnformatted(node);
}

void ehem_json_string_free(char *s)
{
    cJSON_free(s); /* pair with cJSON's allocator; NULL-safe */
}

bool ehem_json_is_object(const ehem_json *node)
{
    return cJSON_IsObject(node);
}

bool ehem_json_is_array(const ehem_json *node)
{
    return cJSON_IsArray(node);
}

size_t ehem_json_array_size(const ehem_json *arr)
{
    int n;
    if (!cJSON_IsArray(arr)) {
        return 0;
    }
    n = cJSON_GetArraySize(arr);
    return (n > 0) ? (size_t)n : 0;
}

const ehem_json *ehem_json_array_get(const ehem_json *arr, size_t i)
{
    if (!cJSON_IsArray(arr) || i > (size_t)INT_MAX) {
        return NULL;
    }
    return cJSON_GetArrayItem(arr, (int)i);
}

bool ehem_json_as_string(const ehem_json *node, const char **out)
{
    if (!cJSON_IsString(node) || node->valuestring == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = node->valuestring;
    }
    return true;
}

bool ehem_json_has(const ehem_json *obj, const char *key)
{
    if (!cJSON_IsObject(obj) || key == NULL) {
        return false;
    }
    return cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}

const ehem_json *ehem_json_get(const ehem_json *obj, const char *key)
{
    if (!cJSON_IsObject(obj) || key == NULL) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

bool ehem_json_get_string(const ehem_json *obj, const char *key, const char **out)
{
    const cJSON *item = ehem_json_get(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = item->valuestring;
    }
    return true;
}

bool ehem_json_get_int64(const ehem_json *obj, const char *key, int64_t *out)
{
    const cJSON *item = ehem_json_get(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    if (out != NULL) {
        /* cJSON stores every number as a double; valueint is clamped to int
         * range, so read the double to preserve values like uptime/timestamps. */
        *out = (int64_t)item->valuedouble;
    }
    return true;
}

bool ehem_json_get_double(const ehem_json *obj, const char *key, double *out)
{
    const cJSON *item = ehem_json_get(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    if (out != NULL) {
        *out = item->valuedouble;
    }
    return true;
}

bool ehem_json_get_bool(const ehem_json *obj, const char *key, bool *out)
{
    const cJSON *item = ehem_json_get(obj, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    if (out != NULL) {
        *out = cJSON_IsTrue(item);
    }
    return true;
}

ehem_json *ehem_json_new_object(void)
{
    return cJSON_CreateObject();
}

bool ehem_json_add_string(ehem_json *obj, const char *key, const char *val)
{
    if (obj == NULL || key == NULL || val == NULL) {
        return false;
    }
    return cJSON_AddStringToObject(obj, key, val) != NULL;
}

bool ehem_json_add_int64(ehem_json *obj, const char *key, int64_t val)
{
    if (obj == NULL || key == NULL) {
        return false;
    }
    return cJSON_AddNumberToObject(obj, key, (double)val) != NULL;
}

ehem_json *ehem_json_add_object(ehem_json *obj, const char *key)
{
    if (obj == NULL || key == NULL) {
        return NULL;
    }
    return cJSON_AddObjectToObject(obj, key);
}
