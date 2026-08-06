/*
 * ejwt.c — base64 codecs + compact eJWT builder.
 *
 * implements: REQ-AUTH-001
 *
 * The eJWT is assembled to be byte-identical to the python client's build_ejwt
 * (encedo-hem-python-api auth.py), locked in by tests/unit/test_ejwt.c against
 * a committed fixture. The compact payload JSON goes through the json helper
 * layer (cJSON stays contained to json.c); the HMAC tag through the crypto
 * shim.
 */
#include "ejwt.h"

#include <stdlib.h>
#include <string.h>

#include "crypto_shim.h"
#include "json.h"

/* The fixed JOSE header — a hardcoded byte string, never re-serialized. */
static const char EJWT_HEADER[] = "{\"ecdh\":\"x25519\",\"alg\":\"HS256\",\"typ\":\"JWT\"}";

static const char B64_STD_ALPHA[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char B64_URL_ALPHA[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t ehem_b64_std_encoded_len(size_t n)
{
    return 4 * ((n + 2) / 3);
}

size_t ehem_b64url_encoded_len(size_t n)
{
    size_t rem = n % 3;
    return (n / 3) * 4 + (rem ? rem + 1 : 0);
}

static size_t b64_encode(const uint8_t *in, size_t in_len, char *out,
                         size_t out_cap, const char *alpha, int pad)
{
    if (out == NULL || (in == NULL && in_len != 0)) {
        return (size_t)-1;
    }
    size_t need = pad ? ehem_b64_std_encoded_len(in_len)
                      : ehem_b64url_encoded_len(in_len);
    if (out_cap < need + 1) {
        return (size_t)-1;
    }

    size_t i = 0, o = 0;
    while (in_len - i >= 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        out[o++] = alpha[(v >> 6) & 63];
        out[o++] = alpha[v & 63];
        i += 3;
    }
    size_t rem = in_len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        if (pad) { out[o++] = '='; out[o++] = '='; }
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        out[o++] = alpha[(v >> 6) & 63];
        if (pad) { out[o++] = '='; }
    }
    out[o] = '\0';
    return o;
}

static size_t b64_decode(const char *in, size_t in_len, uint8_t *out,
                         size_t out_cap, const char *alpha)
{
    if (out == NULL || (in == NULL && in_len != 0)) {
        return (size_t)-1;
    }
    signed char rev[256];
    memset(rev, -1, sizeof rev);
    for (int i = 0; i < 64; i++) {
        rev[(unsigned char)alpha[i]] = (signed char)i;
    }

    /* Trailing '=' padding is accepted for either variant. */
    while (in_len > 0 && in[in_len - 1] == '=') {
        in_len--;
    }
    if (in_len % 4 == 1) {   /* one leftover char cannot encode any byte */
        return (size_t)-1;
    }

    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < in_len; i++) {
        int v = rev[(unsigned char)in[i]];
        if (v < 0) {
            return (size_t)-1;   /* char outside this alphabet */
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_cap) {
                return (size_t)-1;
            }
            out[o++] = (uint8_t)((acc >> bits) & 0xff);
        }
    }
    return o;
}

size_t ehem_b64_std_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap)
{
    return b64_encode(in, in_len, out, out_cap, B64_STD_ALPHA, 1);
}

size_t ehem_b64url_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap)
{
    return b64_encode(in, in_len, out, out_cap, B64_URL_ALPHA, 0);
}

size_t ehem_b64_std_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    return b64_decode(in, in_len, out, out_cap, B64_STD_ALPHA);
}

size_t ehem_b64url_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    return b64_decode(in, in_len, out, out_cap, B64_URL_ALPHA);
}

ehem_rc ehem_ejwt_build(const char *jti, const char *spk,
                        const char *scope,
                        const uint8_t user_pub[32], const uint8_t shared[32],
                        int64_t now, int64_t requested_exp,
                        char **out_ejwt)
{
    if (out_ejwt == NULL) {
        return EHEM_ERR_ARG;
    }
    *out_ejwt = NULL;   /* cleared on every error path (ehem_ctx_create convention) */
    if (jti == NULL || spk == NULL || scope == NULL ||
        user_pub == NULL || shared == NULL) {
        return EHEM_ERR_ARG;
    }

    /* The token's `exp` claim is the requested lifetime verbatim. It is NOT
     * capped at the challenge's `exp`: firmware treats challenge.exp as the
     * response DEADLINE (enforced separately by the time-based `jti` nonce), and
     * copies our `exp` straight into the issued bearer (STEP-M2-045). Capping at
     * the ~60 s challenge deadline is what produced ~60 s bearers + re-login per
     * call. The eJWT itself must still be un-expired when it reaches the device,
     * which requested_exp (now + lifetime) always is. */
    int64_t exp = requested_exp;

    /* iss = user public key in STANDARD base64 (with padding). 32 → 44 chars. */
    char iss[64];
    if (ehem_b64_std_encode(user_pub, EHEM_X25519_KEYSIZE, iss, sizeof iss) == (size_t)-1) {
        return EHEM_ERR_PROTOCOL;
    }

    /* Compact payload JSON, keys in order jti/aud/exp/iat/iss/scope. */
    ehem_json *obj = ehem_json_new_object();
    if (obj == NULL) {
        return EHEM_ERR_NOMEM;
    }
    bool ok = ehem_json_add_string(obj, "jti", jti) &&
              ehem_json_add_string(obj, "aud", spk) &&
              ehem_json_add_int64 (obj, "exp", exp) &&
              ehem_json_add_int64 (obj, "iat", now) &&
              ehem_json_add_string(obj, "iss", iss) &&
              ehem_json_add_string(obj, "scope", scope);
    char *payload_json = ok ? ehem_json_print(obj) : NULL;
    ehem_json_free(obj);
    if (payload_json == NULL) {
        return EHEM_ERR_NOMEM;
    }

    /* Header segment (base64url, no pad) — the header bytes are constant. */
    char header_seg[128];
    size_t header_n = ehem_b64url_encode((const uint8_t *)EJWT_HEADER,
                                         sizeof EJWT_HEADER - 1,
                                         header_seg, sizeof header_seg);

    /* Payload segment (base64url, no pad). */
    size_t payload_len = strlen(payload_json);
    size_t payload_seg_cap = ehem_b64url_encoded_len(payload_len) + 1;
    char *payload_seg = malloc(payload_seg_cap);
    size_t payload_n = payload_seg
        ? ehem_b64url_encode((const uint8_t *)payload_json, payload_len,
                             payload_seg, payload_seg_cap)
        : (size_t)-1;
    ehem_json_string_free(payload_json);
    if (header_n == (size_t)-1 || payload_seg == NULL || payload_n == (size_t)-1) {
        free(payload_seg);
        return payload_seg == NULL ? EHEM_ERR_NOMEM : EHEM_ERR_PROTOCOL;
    }

    /* Final token: "<header>.<payload>.<sig>". The signing input is the first
     * two segments joined by '.'; the signature is HMAC-SHA256 over it, keyed
     * with the shared secret, base64url no-pad (32 bytes → 43 chars). */
    size_t sign_len = header_n + 1 + payload_n;
    size_t sig_len = ehem_b64url_encoded_len(EHEM_SHA256_SIZE);   /* 43 */
    char *ejwt = malloc(sign_len + 1 + sig_len + 1);
    if (ejwt == NULL) {
        free(payload_seg);
        return EHEM_ERR_NOMEM;
    }

    memcpy(ejwt, header_seg, header_n);
    ejwt[header_n] = '.';
    memcpy(ejwt + header_n + 1, payload_seg, payload_n);
    free(payload_seg);

    uint8_t mac[EHEM_SHA256_SIZE];
    ehem_rc rc = ehem_hmac_sha256(shared, EHEM_X25519_KEYSIZE,
                                  (const uint8_t *)ejwt, sign_len, mac);
    if (rc != EHEM_OK) {
        free(ejwt);
        return rc;
    }

    ejwt[sign_len] = '.';
    if (ehem_b64url_encode(mac, EHEM_SHA256_SIZE,
                           ejwt + sign_len + 1, sig_len + 1) == (size_t)-1) {
        free(ejwt);
        return EHEM_ERR_PROTOCOL;
    }

    *out_ejwt = ejwt;
    return EHEM_OK;
}

void ehem_ejwt_free(char *ejwt)
{
    free(ejwt);
}
