/*
 * proto_crypto.c — bindings for the `crypto` API group: exdsa signing (M4),
 * exdsa verification, ECDH, HMAC, AES cipher (M6).
 *
 * implements: REQ-OPS-001, REQ-OPS-003, REQ-OPS-004, REQ-OPS-005,
 *             REQ-OPS-006
 *
 * Follows the proto_keymgmt.c template: pre-validate → build the JSON body →
 * send through the shared request path (proto_common — per-KID bearer,
 * REQ-NET-005 recovery, HTTP→rc mapping) → decode the response into a
 * caller-owned struct. The signature bytes are returned exactly as the
 * device produced them (DER for ECDSA, raw for EdDSA — REQ-OPS-001).
 */
#include "ehem/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "crypto_shim.h"     /* ehem_zeroize for secret-bearing outputs */
#include "ejwt.h"            /* ehem_b64_std_encode/decode */
#include "json.h"
#include "proto_common.h"
#include "transport.h"

/* Base64-encode `raw` into a fresh NUL-terminated string (caller frees), or
 * NULL on allocation failure. */
static char *b64_dup(const uint8_t *raw, size_t raw_len)
{
    size_t enc = ehem_b64_std_encoded_len(raw_len);
    char *s = malloc(enc + 1);
    if (s != NULL && ehem_b64_std_encode(raw, raw_len, s, enc + 1) == (size_t)-1) {
        free(s);
        s = NULL;
    }
    return s;
}

/*
 * Validate the ext_kid/pubkey peer-selection arguments shared by the ECDH-
 * capable endpoints (ecdh; hmac and cipher in their derived modes). Exactly
 * one peer must be given — except when `allow_none` (hmac/cipher direct
 * mode). Returns EHEM_OK or fails the context with EHEM_ERR_ARG.
 */
static ehem_rc check_peer_args(ehem_ctx *ctx, const char *what,
                               const char *ext_kid,
                               const uint8_t *pubkey, size_t pubkey_len,
                               int allow_none)
{
    int have_ext = (ext_kid != NULL);
    int have_pub = (pubkey != NULL || pubkey_len != 0);

    if (have_ext && have_pub) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "%s: give either ext_kid or pubkey, not both",
                             what);
    }
    if (!have_ext && !have_pub && !allow_none) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "%s: a peer is required (ext_kid or pubkey)",
                             what);
    }
    if (have_ext && !ehem_proto_is_kid_hex(ext_kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "%s: ext_kid must be exactly 32 hex chars", what);
    }
    if (have_pub &&
        (pubkey == NULL || pubkey_len < 1 ||
         pubkey_len > EHEM_ECDH_PUBKEY_MAX)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "%s: pubkey must be 1..%d bytes", what,
                             EHEM_ECDH_PUBKEY_MAX);
    }
    return EHEM_OK;
}

/* Add the optional peer fields to a request body (NULLs skip cleanly). */
static bool add_peer_fields(ehem_json *body_obj, const char *ext_kid,
                            const uint8_t *pubkey, size_t pubkey_len)
{
    if (ext_kid != NULL && !ehem_json_add_string(body_obj, "ext_kid", ext_kid)) {
        return false;
    }
    if (pubkey != NULL && pubkey_len > 0) {
        char *b64 = b64_dup(pubkey, pubkey_len);
        bool ok = (b64 != NULL) &&
                  ehem_json_add_string(body_obj, "pubkey", b64);
        free(b64);
        if (!ok) {
            return false;
        }
    }
    return true;
}

ehem_rc ehem_sign(ehem_ctx *ctx, const char *kid, const char *alg,
                  const uint8_t *msg, size_t msg_len,
                  const uint8_t *sig_ctx, size_t sig_ctx_len,
                  ehem_signature **out)
{
    char scope[64];
    char *field = NULL;
    char *body;
    ehem_json *body_obj;
    ehem_json *root = NULL;
    ehem_signature *result;
    const char *sign_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || alg == NULL || msg == NULL ||
        out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/sign: kid must be exactly 32 hex chars");
    }
    if (alg[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/sign: alg must be non-empty");
    }
    if (msg_len < 1 || msg_len > EHEM_SIGN_MSG_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/sign: msg must be 1..%d bytes "
                             "(the device rejects an empty message)",
                             EHEM_SIGN_MSG_MAX);
    }
    if ((sig_ctx == NULL && sig_ctx_len != 0) ||
        sig_ctx_len > EHEM_SIGN_SIG_CTX_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/sign: sig_ctx must be at most %d bytes",
                             EHEM_SIGN_SIG_CTX_MAX);
    }

    /* Body {kid,msg(b64),alg[,ctx(b64)]} in the doc's field order. */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_add_string(body_obj, "kid", kid)) {
        goto oom_obj;
    }
    if ((field = b64_dup(msg, msg_len)) == NULL ||
        !ehem_json_add_string(body_obj, "msg", field)) {
        goto oom_obj;
    }
    free(field);
    field = NULL;
    if (!ehem_json_add_string(body_obj, "alg", alg)) {
        goto oom_obj;
    }
    if (sig_ctx != NULL && sig_ctx_len > 0) {
        if ((field = b64_dup(sig_ctx, sig_ctx_len)) == NULL ||
            !ehem_json_add_string(body_obj, "ctx", field)) {
            goto oom_obj;
        }
        free(field);
        field = NULL;
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Exact per-key scope — shared with ehem_key_get's cache entry
     * (REQ-OPS-001; firmware strcmp, no prefix acceptance). */
    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/crypto/exdsa/sign",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;   /* 403 → SCOPE_DENIED; 400/406 → DEVICE with payload */
    }

    if (!ehem_json_get_string(root, "sign", &sign_b64) ||
        sign_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/sign: response missing 'sign'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(sign_b64);
    if (result != NULL) {
        result->sig = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->sig == NULL) {
        ehem_signature_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(sign_b64, b64_len, result->sig, b64_len);
    ehem_json_free(root);
    if (n == (size_t)-1 || n == 0) {
        ehem_signature_free(result);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/sign: 'sign' is not valid base64");
    }
    result->sig_len = n;

    *out = result;
    return EHEM_OK;

oom_obj:
    free(field);
    ehem_json_free(body_obj);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

void ehem_signature_free(ehem_signature *sig)
{
    if (sig == NULL) {
        return;
    }
    free(sig->sig);
    free(sig);
}

ehem_rc ehem_verify(ehem_ctx *ctx, const char *kid, const char *alg,
                    const uint8_t *msg, size_t msg_len,
                    const uint8_t *sig_ctx, size_t sig_ctx_len,
                    const uint8_t *sig, size_t sig_len)
{
    char scope[64];
    char *field = NULL;
    char *body;
    char *resp = NULL;
    ehem_json *body_obj;
    long status;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || alg == NULL || msg == NULL ||
        sig == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/verify: kid must be exactly 32 hex chars");
    }
    if (alg[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/verify: alg must be non-empty");
    }
    if (msg_len < 1 || msg_len > EHEM_SIGN_MSG_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/verify: msg must be 1..%d bytes",
                             EHEM_SIGN_MSG_MAX);
    }
    if (sig_len < 1 || sig_len > EHEM_VERIFY_SIG_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/verify: sig must be 1..%d bytes",
                             EHEM_VERIFY_SIG_MAX);
    }
    if ((sig_ctx == NULL && sig_ctx_len != 0) ||
        sig_ctx_len > EHEM_SIGN_SIG_CTX_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/verify: sig_ctx must be at most %d bytes",
                             EHEM_SIGN_SIG_CTX_MAX);
    }

    /* Body {kid,msg(b64),sign(b64),alg[,ctx(b64)]} in the doc's field order. */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_add_string(body_obj, "kid", kid)) {
        goto oom_obj;
    }
    if ((field = b64_dup(msg, msg_len)) == NULL ||
        !ehem_json_add_string(body_obj, "msg", field)) {
        goto oom_obj;
    }
    free(field);
    field = NULL;
    if ((field = b64_dup(sig, sig_len)) == NULL ||
        !ehem_json_add_string(body_obj, "sign", field)) {
        goto oom_obj;
    }
    free(field);
    field = NULL;
    if (!ehem_json_add_string(body_obj, "alg", alg)) {
        goto oom_obj;
    }
    if (sig_ctx != NULL && sig_ctx_len > 0) {
        if ((field = b64_dup(sig_ctx, sig_ctx_len)) == NULL ||
            !ehem_json_add_string(body_obj, "ctx", field)) {
            goto oom_obj;
        }
        free(field);
        field = NULL;
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_POST, "/api/crypto/exdsa/verify",
                                body, scope, EHEM_TLS_REQ_DEFAULT, &resp);
    ehem_json_string_free(body);
    if (rc == EHEM_OK) {
        free(resp);             /* valid: empty 200; tolerate a body anyway */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }

    /* The device reports "signature valid" as an EMPTY 200, which the shared
     * path surfaces as EHEM_ERR_PROTOCOL with http_status 200 — the same
     * success shape as delete/reboot. Anything else (406 = invalid signature /
     * wrong key type / kid not found, indistinguishable — REQ-OPS-003) passes
     * through already mapped and recorded. */
    status = ehem_last_error(ctx)->http_status;
    if (rc == EHEM_ERR_PROTOCOL && status == 200) {
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }
    return rc;

oom_obj:
    free(field);
    ehem_json_free(body_obj);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_ecdh(ehem_ctx *ctx, const char *kid,
                  const char *ext_kid,
                  const uint8_t *pubkey, size_t pubkey_len,
                  const char *alg,
                  ehem_ecdh_secret **out)
{
    char scope[64];
    char *body;
    ehem_json *body_obj;
    ehem_json *root = NULL;
    ehem_ecdh_secret *result;
    const char *ecdh_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/ecdh: kid must be exactly 32 hex chars");
    }
    rc = check_peer_args(ctx, "crypto/ecdh", ext_kid, pubkey, pubkey_len, 0);
    if (rc != EHEM_OK) {
        return rc;
    }
    if (alg != NULL && alg[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/ecdh: alg must be non-empty when given");
    }

    /* Body {kid[,ext_kid|pubkey][,alg]} in the doc's field order. */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!ehem_json_add_string(body_obj, "kid", kid) ||
        !add_peer_fields(body_obj, ext_kid, pubkey, pubkey_len) ||
        (alg != NULL && !ehem_json_add_string(body_obj, "alg", alg))) {
        ehem_json_free(body_obj);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/crypto/ecdh",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;   /* 403 → SCOPE_DENIED; 400/406 → DEVICE with payload */
    }

    if (!ehem_json_get_string(root, "ecdh", &ecdh_b64) || ecdh_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/ecdh: response missing 'ecdh'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(ecdh_b64);
    if (result != NULL) {
        result->secret = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->secret == NULL) {
        ehem_ecdh_secret_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(ecdh_b64, b64_len, result->secret, b64_len);
    ehem_json_free(root);
    if (n == (size_t)-1 || n == 0) {
        ehem_zeroize(result->secret, b64_len);   /* partial decode residue */
        ehem_ecdh_secret_free(result);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/ecdh: 'ecdh' is not valid base64");
    }
    result->secret_len = n;

    *out = result;
    return EHEM_OK;
}

void ehem_ecdh_secret_free(ehem_ecdh_secret *s)
{
    if (s == NULL) {
        return;
    }
    if (s->secret != NULL) {
        /* The response carried key material — scrub before releasing
         * (ARCHITECTURE §4; the JSON/transport buffers holding the base64
         * copy are freed by their owners above). */
        ehem_zeroize(s->secret, s->secret_len);
        free(s->secret);
    }
    free(s);
}

/*
 * Shared pre-validation + body construction for the two hmac endpoints.
 * `mac` NULL → hash body {kid,msg[,alg][,peer]}; non-NULL → verify body
 * {kid,msg,mac[,alg][,peer]}. On failure the context error is recorded and
 * NULL is returned.
 */
static char *hmac_build(ehem_ctx *ctx, const char *what,
                        const char *kid, const char *alg,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *mac, size_t mac_len,
                        const char *ext_kid,
                        const uint8_t *pubkey, size_t pubkey_len,
                        ehem_rc *rc_out)
{
    char *field = NULL;
    char *body;
    ehem_json *body_obj;
    int derived = (ext_kid != NULL || pubkey != NULL || pubkey_len != 0);

    *rc_out = EHEM_ERR_ARG;

    if (!ehem_proto_is_kid_hex(kid)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: kid must be exactly 32 hex chars", what);
        return NULL;
    }
    if (msg_len < 1 || msg_len > EHEM_SIGN_MSG_MAX) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: msg must be 1..%d bytes", what, EHEM_SIGN_MSG_MAX);
        return NULL;
    }
    if (mac != NULL && (mac_len < 1 || mac_len > EHEM_HMAC_MAC_MAX)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: mac must be 1..%d bytes", what, EHEM_HMAC_MAC_MAX);
        return NULL;
    }
    if (check_peer_args(ctx, what, ext_kid, pubkey, pubkey_len, 1) != EHEM_OK) {
        return NULL;
    }
    if (alg == NULL && derived) {
        /* Firmware falls through to a bare 406 without an alg in the
         * ECDH-derived flow — catch it client-side (REQ-OPS-005). */
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: alg is required in the ECDH-derived flow", what);
        return NULL;
    }
    if (alg != NULL && alg[0] == '\0') {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: alg must be non-empty when given", what);
        return NULL;
    }

    *rc_out = EHEM_ERR_NOMEM;
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    if (!ehem_json_add_string(body_obj, "kid", kid)) {
        goto oom;
    }
    if ((field = b64_dup(msg, msg_len)) == NULL ||
        !ehem_json_add_string(body_obj, "msg", field)) {
        goto oom;
    }
    free(field);
    field = NULL;
    if (mac != NULL) {
        if ((field = b64_dup(mac, mac_len)) == NULL ||
            !ehem_json_add_string(body_obj, "mac", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }
    if (alg != NULL && !ehem_json_add_string(body_obj, "alg", alg)) {
        goto oom;
    }
    if (!add_peer_fields(body_obj, ext_kid, pubkey, pubkey_len)) {
        goto oom;
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    *rc_out = EHEM_OK;
    return body;

oom:
    free(field);
    ehem_json_free(body_obj);
    ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    return NULL;
}

ehem_rc ehem_hmac(ehem_ctx *ctx, const char *kid, const char *alg,
                  const uint8_t *msg, size_t msg_len,
                  const char *ext_kid,
                  const uint8_t *pubkey, size_t pubkey_len,
                  ehem_mac **out)
{
    char scope[64];
    char *body;
    ehem_json *root = NULL;
    ehem_mac *result;
    const char *mac_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || msg == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    body = hmac_build(ctx, "crypto/hmac/hash", kid, alg, msg, msg_len,
                      NULL, 0, ext_kid, pubkey, pubkey_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/crypto/hmac/hash",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    if (!ehem_json_get_string(root, "mac", &mac_b64) || mac_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/hmac/hash: response missing 'mac'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(mac_b64);
    if (result != NULL) {
        result->mac = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->mac == NULL) {
        ehem_mac_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(mac_b64, b64_len, result->mac, b64_len);
    ehem_json_free(root);
    if (n == (size_t)-1 || n == 0) {
        ehem_mac_free(result);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/hmac/hash: 'mac' is not valid base64");
    }
    result->mac_len = n;

    *out = result;
    return EHEM_OK;
}

void ehem_mac_free(ehem_mac *m)
{
    if (m == NULL) {
        return;
    }
    free(m->mac);
    free(m);
}

ehem_rc ehem_hmac_verify(ehem_ctx *ctx, const char *kid, const char *alg,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *mac, size_t mac_len,
                         const char *ext_kid,
                         const uint8_t *pubkey, size_t pubkey_len)
{
    char scope[64];
    char *body;
    char *resp = NULL;
    long status;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || msg == NULL || mac == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    body = hmac_build(ctx, "crypto/hmac/verify", kid, alg, msg, msg_len,
                      mac, mac_len, ext_kid, pubkey, pubkey_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_POST, "/api/crypto/hmac/verify",
                                body, scope, EHEM_TLS_REQ_DEFAULT, &resp);
    ehem_json_string_free(body);
    if (rc == EHEM_OK) {
        free(resp);             /* valid: empty 200; tolerate a body anyway */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }

    /* Valid MAC = EMPTY 200 (same success shape as exdsa/verify). 406 = MAC
     * mismatch / wrong key type / ECDH failure, indistinguishable. */
    status = ehem_last_error(ctx)->http_status;
    if (rc == EHEM_ERR_PROTOCOL && status == 200) {
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }
    return rc;
}

/*
 * Shared pre-validation + body construction for the two cipher endpoints.
 * `iv`/`tag` non-NULL → decrypt body fields. On failure the context error
 * is recorded and NULL returned (rc in *rc_out).
 */
static char *cipher_build(ehem_ctx *ctx, const char *what,
                          const char *kid, const char *alg,
                          const uint8_t *msg, size_t msg_len, size_t msg_max,
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *tag, size_t tag_len,
                          const uint8_t *aad, size_t aad_len,
                          const char *ext_kid,
                          const uint8_t *pubkey, size_t pubkey_len,
                          const uint8_t *hkdf_ctx, size_t hkdf_ctx_len,
                          ehem_rc *rc_out)
{
    char *field = NULL;
    char *body;
    ehem_json *body_obj;

    *rc_out = EHEM_ERR_ARG;

    if (!ehem_proto_is_kid_hex(kid)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: kid must be exactly 32 hex chars", what);
        return NULL;
    }
    if (strlen(alg) != 10) {
        /* The device hard-rejects any other length; literals themselves are
         * the device's to validate (REQ-OPS-006). */
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: alg must be one of the 10-char AES selectors",
                      what);
        return NULL;
    }
    if (msg_len < 1 || msg_len > msg_max) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: msg must be 1..%zu bytes", what, msg_max);
        return NULL;
    }
    if ((iv != NULL || iv_len != 0) &&
        (iv == NULL || iv_len != EHEM_CIPHER_IV_LEN)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: iv must be exactly %d bytes when given", what,
                      EHEM_CIPHER_IV_LEN);
        return NULL;
    }
    if ((tag != NULL || tag_len != 0) &&
        (tag == NULL || tag_len != EHEM_CIPHER_TAG_LEN)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: tag must be exactly %d bytes when given", what,
                      EHEM_CIPHER_TAG_LEN);
        return NULL;
    }
    if ((aad == NULL && aad_len != 0) || aad_len > EHEM_CIPHER_AAD_MAX) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: aad must be at most %d bytes", what,
                      EHEM_CIPHER_AAD_MAX);
        return NULL;
    }
    if (check_peer_args(ctx, what, ext_kid, pubkey, pubkey_len, 1) != EHEM_OK) {
        return NULL;
    }
    if ((hkdf_ctx == NULL && hkdf_ctx_len != 0) ||
        hkdf_ctx_len > EHEM_CIPHER_HKDF_CTX_MAX) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: hkdf_ctx must be at most %d bytes", what,
                      EHEM_CIPHER_HKDF_CTX_MAX);
        return NULL;
    }

    *rc_out = EHEM_ERR_NOMEM;
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    if (!ehem_json_add_string(body_obj, "kid", kid)) {
        goto oom;
    }
    if ((field = b64_dup(msg, msg_len)) == NULL ||
        !ehem_json_add_string(body_obj, "msg", field)) {
        goto oom;
    }
    free(field);
    field = NULL;
    if (!ehem_json_add_string(body_obj, "alg", alg)) {
        goto oom;
    }
    if (iv != NULL) {
        if ((field = b64_dup(iv, iv_len)) == NULL ||
            !ehem_json_add_string(body_obj, "iv", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }
    if (tag != NULL) {
        if ((field = b64_dup(tag, tag_len)) == NULL ||
            !ehem_json_add_string(body_obj, "tag", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }
    if (aad != NULL && aad_len > 0) {
        if ((field = b64_dup(aad, aad_len)) == NULL ||
            !ehem_json_add_string(body_obj, "aad", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }
    if (!add_peer_fields(body_obj, ext_kid, pubkey, pubkey_len)) {
        goto oom;
    }
    if (hkdf_ctx != NULL && hkdf_ctx_len > 0) {
        if ((field = b64_dup(hkdf_ctx, hkdf_ctx_len)) == NULL ||
            !ehem_json_add_string(body_obj, "ctx", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    *rc_out = EHEM_OK;
    return body;

oom:
    free(field);
    ehem_json_free(body_obj);
    ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    return NULL;
}

/* Decode an optional fixed-size base64 response field (iv/tag) in place;
 * returns false on a malformed value (wrong size / bad base64). */
static bool decode_fixed_field(ehem_json *root, const char *name,
                               uint8_t *out, size_t want_len, int *has)
{
    const char *b64;
    uint8_t buf[32];
    size_t n;

    *has = 0;
    if (!ehem_json_get_string(root, name, &b64) || b64[0] == '\0') {
        return true;   /* absent — the mode decides whether that is OK */
    }
    n = ehem_b64_std_decode(b64, strlen(b64), buf, sizeof buf);
    if (n != want_len) {
        return false;
    }
    memcpy(out, buf, want_len);
    *has = 1;
    return true;
}

ehem_rc ehem_encrypt(ehem_ctx *ctx, const char *kid, const char *alg,
                     const uint8_t *msg, size_t msg_len,
                     const uint8_t *aad, size_t aad_len,
                     const char *ext_kid,
                     const uint8_t *pubkey, size_t pubkey_len,
                     const uint8_t *hkdf_ctx, size_t hkdf_ctx_len,
                     ehem_ciphertext **out)
{
    char scope[64];
    char *body;
    ehem_json *root = NULL;
    ehem_ciphertext *result;
    const char *ct_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || alg == NULL || msg == NULL ||
        out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    body = cipher_build(ctx, "crypto/cipher/encrypt", kid, alg,
                        msg, msg_len, EHEM_CIPHER_MSG_MAX,
                        NULL, 0, NULL, 0,               /* no iv/tag fields */
                        aad, aad_len, ext_kid, pubkey, pubkey_len,
                        hkdf_ctx, hkdf_ctx_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST,
                                 "/api/crypto/cipher/encrypt",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    if (!ehem_json_get_string(root, "ciphertext", &ct_b64) ||
        ct_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/cipher/encrypt: response missing "
                             "'ciphertext'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(ct_b64);
    if (result != NULL) {
        result->ciphertext = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->ciphertext == NULL) {
        ehem_ciphertext_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(ct_b64, b64_len, result->ciphertext, b64_len);
    if (n == (size_t)-1 || n == 0) {
        goto bad_field;
    }
    result->ciphertext_len = n;

    if (!decode_fixed_field(root, "iv", result->iv, EHEM_CIPHER_IV_LEN,
                            &result->has_iv) ||
        !decode_fixed_field(root, "tag", result->tag, EHEM_CIPHER_TAG_LEN,
                            &result->has_tag)) {
        goto bad_field;
    }
    ehem_json_free(root);

    *out = result;
    return EHEM_OK;

bad_field:
    ehem_ciphertext_free(result);
    ehem_json_free(root);
    return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                         "crypto/cipher/encrypt: malformed response field");
}

void ehem_ciphertext_free(ehem_ciphertext *c)
{
    if (c == NULL) {
        return;
    }
    free(c->ciphertext);
    free(c);
}

ehem_rc ehem_decrypt(ehem_ctx *ctx, const char *kid, const char *alg,
                     const uint8_t *msg, size_t msg_len,
                     const uint8_t *iv, size_t iv_len,
                     const uint8_t *tag, size_t tag_len,
                     const uint8_t *aad, size_t aad_len,
                     const char *ext_kid,
                     const uint8_t *pubkey, size_t pubkey_len,
                     const uint8_t *hkdf_ctx, size_t hkdf_ctx_len,
                     ehem_plaintext **out)
{
    char scope[64];
    char *body;
    ehem_json *root = NULL;
    ehem_plaintext *result;
    const char *pt_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || alg == NULL || msg == NULL ||
        out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    body = cipher_build(ctx, "crypto/cipher/decrypt", kid, alg,
                        msg, msg_len, EHEM_CIPHER_CT_MAX,
                        iv, iv_len, tag, tag_len,
                        aad, aad_len, ext_kid, pubkey, pubkey_len,
                        hkdf_ctx, hkdf_ctx_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST,
                                 "/api/crypto/cipher/decrypt",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    if (!ehem_json_get_string(root, "plaintext", &pt_b64)) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/cipher/decrypt: response missing "
                             "'plaintext'");
    }

    /* A zero-length plaintext is legal (an external all-padding CBC block),
     * so an empty string decodes to len 0 rather than erroring. */
    result = calloc(1, sizeof *result);
    b64_len = strlen(pt_b64);
    if (result != NULL) {
        result->plaintext = malloc(b64_len > 0 ? b64_len : 1);
    }
    if (result == NULL || result->plaintext == NULL) {
        ehem_plaintext_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (b64_len > 0) {
        n = ehem_b64_std_decode(pt_b64, b64_len, result->plaintext, b64_len);
        if (n == (size_t)-1) {
            ehem_zeroize(result->plaintext, b64_len);
            ehem_plaintext_free(result);
            ehem_json_free(root);
            return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                                 "crypto/cipher/decrypt: 'plaintext' is not "
                                 "valid base64");
        }
        result->plaintext_len = n;
    }
    ehem_json_free(root);

    *out = result;
    return EHEM_OK;
}

void ehem_plaintext_free(ehem_plaintext *p)
{
    if (p == NULL) {
        return;
    }
    if (p->plaintext != NULL) {
        ehem_zeroize(p->plaintext, p->plaintext_len);
        free(p->plaintext);
    }
    free(p);
}

/* implements: REQ-OPS-007, REQ-OPS-008 (PQC bindings below; the file header
 * tag block lists the whole group) */

/* Copy an optional response `alg` name (best-effort, truncated; empty when
 * absent — on mlkem/decaps fw v1.2.2 echoes an unwritten buffer). */
static void copy_alg(ehem_json *root, char out[EHEM_PQC_ALG_SIZE])
{
    const char *alg;

    out[0] = '\0';
    if (ehem_json_get_string(root, "alg", &alg)) {
        snprintf(out, EHEM_PQC_ALG_SIZE, "%s", alg);
    }
}

/* Decode a required fixed-length base64 field (the 32-byte ss). */
static bool decode_ss(ehem_json *root, uint8_t ss[EHEM_MLKEM_SS_LEN])
{
    const char *b64;
    uint8_t buf[EHEM_MLKEM_SS_LEN + 4];
    size_t n;

    if (!ehem_json_get_string(root, "ss", &b64) || b64[0] == '\0') {
        return false;
    }
    n = ehem_b64_std_decode(b64, strlen(b64), buf, sizeof buf);
    if (n != EHEM_MLKEM_SS_LEN) {
        ehem_zeroize(buf, sizeof buf);
        return false;
    }
    memcpy(ss, buf, EHEM_MLKEM_SS_LEN);
    ehem_zeroize(buf, sizeof buf);
    return true;
}

ehem_rc ehem_mlkem_encaps(ehem_ctx *ctx, const char *kid,
                          ehem_mlkem_encaps_result **out)
{
    char scope[64];
    char *body;
    ehem_json *body_obj;
    ehem_json *root = NULL;
    ehem_mlkem_encaps_result *result;
    const char *ct_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/mlkem/encaps: kid must be exactly 32 "
                             "hex chars");
    }

    body_obj = ehem_json_new_object();
    if (body_obj == NULL ||
        !ehem_json_add_string(body_obj, "kid", kid)) {
        ehem_json_free(body_obj);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST,
                                 "/api/crypto/pqc/mlkem/encaps",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    if (!ehem_json_get_string(root, "ct", &ct_b64) || ct_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/mlkem/encaps: response missing 'ct'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(ct_b64);
    if (result != NULL) {
        result->ct = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->ct == NULL) {
        ehem_mlkem_encaps_result_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(ct_b64, b64_len, result->ct, b64_len);
    if (n == (size_t)-1 || n == 0 || !decode_ss(root, result->ss)) {
        ehem_mlkem_encaps_result_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/mlkem/encaps: malformed 'ct'/'ss'");
    }
    result->ct_len = n;
    copy_alg(root, result->alg);
    ehem_json_free(root);

    *out = result;
    return EHEM_OK;
}

void ehem_mlkem_encaps_result_free(ehem_mlkem_encaps_result *r)
{
    if (r == NULL) {
        return;
    }
    ehem_zeroize(r->ss, sizeof r->ss);
    free(r->ct);
    free(r);
}

ehem_rc ehem_mlkem_decaps(ehem_ctx *ctx, const char *kid,
                          const uint8_t *ct, size_t ct_len,
                          ehem_mlkem_secret **out)
{
    char scope[64];
    char *field;
    char *body;
    ehem_json *body_obj;
    ehem_json *root = NULL;
    ehem_mlkem_secret *result;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || ct == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/mlkem/decaps: kid must be exactly 32 "
                             "hex chars");
    }
    if (ct_len < 1 || ct_len > EHEM_MLKEM_CT_MAX) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/mlkem/decaps: ct must be 1..%d bytes "
                             "(the key's set demands its exact size)",
                             EHEM_MLKEM_CT_MAX);
    }

    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    field = b64_dup(ct, ct_len);
    if (!ehem_json_add_string(body_obj, "kid", kid) ||
        field == NULL ||
        !ehem_json_add_string(body_obj, "ct", field)) {
        free(field);
        ehem_json_free(body_obj);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    free(field);
    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST,
                                 "/api/crypto/pqc/mlkem/decaps",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    result = calloc(1, sizeof *result);
    if (result == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!decode_ss(root, result->ss)) {
        ehem_mlkem_secret_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/mlkem/decaps: response missing/bad 'ss'");
    }
    copy_alg(root, result->alg);
    ehem_json_free(root);

    *out = result;
    return EHEM_OK;
}

void ehem_mlkem_secret_free(ehem_mlkem_secret *s)
{
    if (s == NULL) {
        return;
    }
    ehem_zeroize(s->ss, sizeof s->ss);
    free(s);
}

/* Shared body builder for the two mldsa endpoints ({kid,msg[,sign][,ctx]}). */
static char *mldsa_build(ehem_ctx *ctx, const char *what,
                         const char *kid,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *sig, size_t sig_len,
                         const uint8_t *sig_ctx, size_t sig_ctx_len,
                         ehem_rc *rc_out)
{
    char *field = NULL;
    char *body;
    ehem_json *body_obj;

    *rc_out = EHEM_ERR_ARG;

    if (!ehem_proto_is_kid_hex(kid)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: kid must be exactly 32 hex chars", what);
        return NULL;
    }
    if (msg_len < 1 || msg_len > EHEM_SIGN_MSG_MAX) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: msg must be 1..%d bytes", what, EHEM_SIGN_MSG_MAX);
        return NULL;
    }
    if (sig != NULL && (sig_len < 1 || sig_len > EHEM_MLDSA_SIG_MAX)) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: sig must be 1..%d bytes", what,
                      EHEM_MLDSA_SIG_MAX);
        return NULL;
    }
    if ((sig_ctx == NULL && sig_ctx_len != 0) ||
        sig_ctx_len > EHEM_SIGN_SIG_CTX_MAX) {
        ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                      "%s: sig_ctx must be at most %d bytes", what,
                      EHEM_SIGN_SIG_CTX_MAX);
        return NULL;
    }

    *rc_out = EHEM_ERR_NOMEM;
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    if (!ehem_json_add_string(body_obj, "kid", kid)) {
        goto oom;
    }
    if ((field = b64_dup(msg, msg_len)) == NULL ||
        !ehem_json_add_string(body_obj, "msg", field)) {
        goto oom;
    }
    free(field);
    field = NULL;
    if (sig != NULL) {
        if ((field = b64_dup(sig, sig_len)) == NULL ||
            !ehem_json_add_string(body_obj, "sign", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }
    if (sig_ctx != NULL && sig_ctx_len > 0) {
        if ((field = b64_dup(sig_ctx, sig_ctx_len)) == NULL ||
            !ehem_json_add_string(body_obj, "ctx", field)) {
            goto oom;
        }
        free(field);
        field = NULL;
    }

    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        return NULL;
    }
    *rc_out = EHEM_OK;
    return body;

oom:
    free(field);
    ehem_json_free(body_obj);
    ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    return NULL;
}

ehem_rc ehem_mldsa_sign(ehem_ctx *ctx, const char *kid,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *sig_ctx, size_t sig_ctx_len,
                        ehem_mldsa_signature **out)
{
    char scope[64];
    char *body;
    ehem_json *root = NULL;
    ehem_mldsa_signature *result;
    const char *sign_b64;
    size_t b64_len;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || msg == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    body = mldsa_build(ctx, "crypto/mldsa/sign", kid, msg, msg_len,
                       NULL, 0, sig_ctx, sig_ctx_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST,
                                 "/api/crypto/pqc/mldsa/sign",
                                 body, scope, EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    if (!ehem_json_get_string(root, "sign", &sign_b64) ||
        sign_b64[0] == '\0') {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/mldsa/sign: response missing 'sign'");
    }

    result = calloc(1, sizeof *result);
    b64_len = strlen(sign_b64);
    if (result != NULL) {
        result->sig = malloc(b64_len);   /* decoded ≤ encoded */
    }
    if (result == NULL || result->sig == NULL) {
        ehem_mldsa_signature_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    n = ehem_b64_std_decode(sign_b64, b64_len, result->sig, b64_len);
    if (n == (size_t)-1 || n == 0) {
        ehem_mldsa_signature_free(result);
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "crypto/mldsa/sign: 'sign' is not valid base64");
    }
    result->sig_len = n;
    copy_alg(root, result->alg);
    ehem_json_free(root);

    *out = result;
    return EHEM_OK;
}

void ehem_mldsa_signature_free(ehem_mldsa_signature *sig)
{
    if (sig == NULL) {
        return;
    }
    free(sig->sig);
    free(sig);
}

ehem_rc ehem_mldsa_verify(ehem_ctx *ctx, const char *kid,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig_ctx, size_t sig_ctx_len,
                          const uint8_t *sig, size_t sig_len)
{
    char scope[64];
    char *body;
    char *resp = NULL;
    long status;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || msg == NULL || sig == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    body = mldsa_build(ctx, "crypto/mldsa/verify", kid, msg, msg_len,
                       sig, sig_len, sig_ctx, sig_ctx_len, &rc);
    if (body == NULL) {
        return rc;
    }

    snprintf(scope, sizeof scope, "keymgmt:use:%s", kid);

    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_POST,
                                "/api/crypto/pqc/mldsa/verify",
                                body, scope, EHEM_TLS_REQ_DEFAULT, &resp);
    ehem_json_string_free(body);
    if (rc == EHEM_OK) {
        free(resp);             /* valid: empty 200; tolerate a body anyway */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }

    status = ehem_last_error(ctx)->http_status;
    if (rc == EHEM_ERR_PROTOCOL && status == 200) {
        /* Valid signature = EMPTY 200 (shared-path empty-body detour). */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }
    if (rc == EHEM_ERR_PROTOCOL && status != 0) {
        /* FW QUIRK (REQ-OPS-008): a failed verify puts the crypto layer's
         * raw error code in the status line, which can land outside
         * 100..599 and map to PROTOCOL above. The device DID answer — this
         * is its (buggy) failure report, not a protocol breakdown. */
        return ehem_ctx_fail(ctx, EHEM_ERR_DEVICE, status, NULL,
                             "crypto/mldsa/verify: device reported failure "
                             "(raw status %ld — fw v1.2.2 emits its crypto "
                             "error code here)", status);
    }
    return rc;
}

/*
 * implements: REQ-OPS-002 — device hardware RNG via the encrypt-IV harvest.
 * Rides the REQ-OPS-006 binding: AES128-CBC over one zero byte returns a
 * fresh device-generated 16-byte IV per call; ciphertexts are discarded.
 */
ehem_rc ehem_random(ehem_ctx *ctx, const char *kid, uint8_t *buf, size_t len)
{
    static const uint8_t throwaway[1] = { 0x00 };
    size_t filled = 0;
    ehem_rc rc;

    if (ctx == NULL || kid == NULL || buf == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    if (!ehem_proto_is_kid_hex(kid)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/random: kid must be exactly 32 hex chars");
    }
    if (len < 1) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "crypto/random: len must be at least 1");
    }

    while (filled < len) {
        ehem_ciphertext *ct = NULL;
        size_t take;

        rc = ehem_encrypt(ctx, kid, EHEM_CIPHER_ALG_AES128_CBC,
                          throwaway, sizeof throwaway, NULL, 0,
                          NULL, NULL, 0, NULL, 0, &ct);
        if (rc != EHEM_OK) {
            ehem_zeroize(buf, filled);   /* no partial entropy on failure */
            return rc;
        }
        if (!ct->has_iv) {
            ehem_ciphertext_free(ct);
            ehem_zeroize(buf, filled);
            return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                                 "crypto/random: encrypt response carried "
                                 "no iv");
        }
        take = len - filled;
        if (take > EHEM_CIPHER_IV_LEN) {
            take = EHEM_CIPHER_IV_LEN;
        }
        memcpy(buf + filled, ct->iv, take);
        filled += take;
        ehem_ciphertext_free(ct);
    }
    return EHEM_OK;
}
