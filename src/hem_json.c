#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "hem/hem_types.h"
#include "internal.h"
#include "cJSON.h"

/* -------------------------------------------------------------------------
 * Response parsing
 * ---------------------------------------------------------------------- */

cJSON *hem_json_parse_response(hem_ctx_t *ctx)
{
    if (!ctx->resp_buf || ctx->resp_len == 0) {
        hem_set_error(ctx, HEM_ERR_JSON, "empty response body");
        return NULL;
    }

    cJSON *root = cJSON_ParseWithLength(ctx->resp_buf, ctx->resp_len);
    if (!root) {
        hem_set_error(ctx, HEM_ERR_JSON, "failed to parse JSON response");
        return NULL;
    }

    return root;
}

/* -------------------------------------------------------------------------
 * Field extraction helpers
 * ---------------------------------------------------------------------- */

void hem_json_get_str(const cJSON *obj, const char *key,
                       char *out, size_t out_size)
{
    out[0] = '\0';
    if (!obj || !key || !out || out_size == 0) return;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out, item->valuestring, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

int64_t hem_json_get_int64(const cJSON *obj, const char *key, int64_t def_val)
{
    if (!obj || !key) return def_val;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item))
        return (int64_t)item->valuedouble;

    return def_val;
}

bool hem_json_get_bool(const cJSON *obj, const char *key, bool def_val)
{
    if (!obj || !key) return def_val;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item))
        return cJSON_IsTrue(item);

    return def_val;
}

/* -------------------------------------------------------------------------
 * Base64 standard encode/decode
 * Used for API message payloads (not JWT -- that uses base64url).
 * ---------------------------------------------------------------------- */

static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *hem_base64_encode(const uint8_t *data, size_t len)
{
    if (!data) return NULL;

    size_t out_len = 4 * ((len + 2) / 3) + 1;
    char *out = malloc(out_len);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    for (; i + 2 < len; i += 3) {
        out[j++] = B64_TABLE[(data[i] >> 2) & 0x3F];
        out[j++] = B64_TABLE[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
        out[j++] = B64_TABLE[((data[i+1] & 0xF) << 2) | ((data[i+2] >> 6) & 0x3)];
        out[j++] = B64_TABLE[data[i+2] & 0x3F];
    }
    if (i < len) {
        out[j++] = B64_TABLE[(data[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = B64_TABLE[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
            out[j++] = B64_TABLE[(data[i+1] & 0xF) << 2];
        } else {
            out[j++] = B64_TABLE[(data[i] & 0x3) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}

static int b64_decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

uint8_t *hem_base64_decode(const char *b64, size_t *out_len)
{
    if (!b64 || !out_len) return NULL;

    size_t in_len = strlen(b64);
    if (in_len % 4 != 0) return NULL;

    *out_len = in_len / 4 * 3;
    if (in_len > 0 && b64[in_len - 1] == '=') (*out_len)--;
    if (in_len > 1 && b64[in_len - 2] == '=') (*out_len)--;

    uint8_t *out = malloc(*out_len);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64_decode_char(b64[i]);
        int b = b64_decode_char(b64[i+1]);
        int c = b64[i+2] == '=' ? 0 : b64_decode_char(b64[i+2]);
        int d = b64[i+3] == '=' ? 0 : b64_decode_char(b64[i+3]);

        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(out);
            *out_len = 0;
            return NULL;
        }

        out[j++] = (uint8_t)((a << 2) | (b >> 4));
        if (b64[i+2] != '=') out[j++] = (uint8_t)(((b & 0xF) << 4) | (c >> 2));
        if (b64[i+3] != '=') out[j++] = (uint8_t)(((c & 0x3) << 6) | d);
    }

    return out;
}
