/*
 * proto_crypto.c — bindings for the `crypto` API group: exdsa signing (M4),
 * exdsa verification, ECDH (M6).
 *
 * implements: REQ-OPS-001, REQ-OPS-003, REQ-OPS-004
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
