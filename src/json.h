/*
 * json.h — internal tolerant-JSON helper layer over vendored cJSON.
 *
 * implements: REQ-BUILD-003 (single place all JSON parsing/serialization goes
 *             through; cJSON stays contained to the JSON codec component —
 *             ARCHITECTURE.md §3, §6 "tolerant parsing")
 *
 * INTERNAL header — not shipped in include/ehem/, never part of the public ABI.
 * The `ehem_json` node is an opaque handle: callers (protocol bindings) include
 * only this header and never see a cJSON type, so cJSON cannot leak into public
 * headers or the export table (REQ-API-006). The concrete type lives in json.c.
 *
 * Tolerant-parsing contract (ARCHITECTURE.md §6): the typed getters treat
 * "absent", "present but wrong type", and "present but null" identically —
 * they return false and leave the caller's output untouched. A protocol
 * binding thus decides per field whether a false is a missing *required* field
 * (→ EHEM_ERR_PROTOCOL) or a legitimately absent optional one; unknown fields
 * are ignored simply by not asking for them.
 */
#ifndef EHEM_JSON_H
#define EHEM_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque JSON value node (backed by vendored cJSON). */
typedef struct cJSON ehem_json;

/*
 * Parse `len` bytes of UTF-8 JSON (need not be NUL-terminated — transport
 * bodies are length-delimited). Returns the root node on success, or NULL on
 * malformed input. Caller owns the result and frees the ROOT with
 * ehem_json_free(); child nodes must not be freed individually.
 */
ehem_json *ehem_json_parse(const char *data, size_t len);

/* Free a document returned by ehem_json_parse. NULL-safe. */
void ehem_json_free(ehem_json *root);

/*
 * Serialize `node` to a freshly allocated, NUL-terminated, unformatted string.
 * Returns NULL on allocation failure. Free with ehem_json_string_free().
 */
char *ehem_json_print(const ehem_json *node);

/* Free a string returned by ehem_json_print. NULL-safe. */
void ehem_json_string_free(char *s);

/* --- structural predicates ------------------------------------------------ */

/* True iff `node` is a JSON object. NULL → false. */
bool ehem_json_is_object(const ehem_json *node);

/* True iff `node` is a JSON array. NULL → false. */
bool ehem_json_is_array(const ehem_json *node);

/* Number of elements in array `arr` (0 if `arr` is NULL or not an array). */
size_t ehem_json_array_size(const ehem_json *arr);

/* The i-th element of array `arr`, or NULL if out of range / not an array. */
const ehem_json *ehem_json_array_get(const ehem_json *arr, size_t i);

/* Read `node` as a string value (for elements already fetched, e.g. array
 * items). Returns true and sets *out only if `node` is a string. */
bool ehem_json_as_string(const ehem_json *node, const char **out);

/* True iff `obj` is an object that contains `key` (any value type, incl null). */
bool ehem_json_has(const ehem_json *obj, const char *key);

/*
 * Fetch a child value by (case-sensitive) key. Returns NULL if `obj` is NULL,
 * not an object, or has no such key. The returned node is owned by the
 * document and stays valid until ehem_json_free(root).
 */
const ehem_json *ehem_json_get(const ehem_json *obj, const char *key);

/* --- typed field getters -------------------------------------------------- */
/*
 * Each returns true and writes *out only when `key` is present in object `obj`
 * AND holds the requested JSON type; otherwise returns false and leaves *out
 * untouched. A returned string points into the document (valid until the root
 * is freed) — copy it if it must outlive the parse.
 */
bool ehem_json_get_string(const ehem_json *obj, const char *key, const char **out);
bool ehem_json_get_int64 (const ehem_json *obj, const char *key, int64_t *out);
bool ehem_json_get_double(const ehem_json *obj, const char *key, double *out);
bool ehem_json_get_bool  (const ehem_json *obj, const char *key, bool *out);

#endif /* EHEM_JSON_H */
