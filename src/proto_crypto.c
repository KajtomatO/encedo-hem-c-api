/*
 * proto_crypto.c — bindings for the `crypto` API group: exdsa signing (M4),
 * exdsa verification (M6).
 *
 * implements: REQ-OPS-001, REQ-OPS-003
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
