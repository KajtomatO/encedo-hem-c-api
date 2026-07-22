/*
 * proto_ext.c — bindings for the ExtAuth (auth/ext) API group: the pairing
 * trio init / validate / mac. The unauthenticated login pair (request /
 * token) joins this file at STEP-M8-030.
 *
 * implements: REQ-AUTH-006, REQ-API-005
 *
 * Firmware ground truth (encedo_firmware api_auth.c): all three endpoints
 * check fls_state==0 and initialised (else 409) before auth; auth is a
 * Bearer with sub=="U" and a scope PREFIX-matching "auth:ext:pair" or
 * "system:config" — the SDK requests the exact scope "auth:ext:pair"
 * through the ordinary ensure-token path, so 401/403 map per REQ-AUTH-003.
 * Claim-value base64 is the STANDARD alphabet with padding (libjwt
 * jwt_Base64encode / wolfSSL Base64_Decode), not base64url.
 */
#include "ehem/auth.h"

#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "ejwt.h"          /* standard-base64 decoder for arg validation */
#include "json.h"
#include "proto_common.h"
#include "transport.h"

#define EXT_PAIR_SCOPE "auth:ext:pair"

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* True iff `s` is standard base64 decoding to exactly 32 bytes (a Curve25519
 * public key or a pairing id). */
static bool is_b64_of_32(const char *s)
{
    uint8_t buf[48];
    size_t n = strlen(s);
    if (n < 43 || n > 46) {   /* 32 bytes → 44 chars padded (43 unpadded) */
        return false;
    }
    return ehem_b64_std_decode(s, n, buf, sizeof buf) == 32;
}

/* Copy REQUIRED string field `key` of `root` into *dst; on a missing field
 * records PROTOCOL detail, on alloc failure NOMEM. Returns EHEM_OK or the
 * failure rc (caller cleans up). */
static ehem_rc req_str(ehem_ctx *ctx, const ehem_json *root, const char *what,
                       const char *key, char **dst)
{
    const char *s;
    if (!ehem_json_get_string(root, key, &s)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "%s: response missing '%s'", what, key);
    }
    *dst = dup_str(s);
    if (*dst == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    return EHEM_OK;
}

/* Serialize {"epk": epk_b64} — shared by init and mac. */
static char *epk_body(const char *epk_b64)
{
    ehem_json *obj = ehem_json_new_object();
    char *body = NULL;
    if (obj != NULL && ehem_json_add_string(obj, "epk", epk_b64)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    return body;
}

/* -------------------------------------------------------------------------- */
/* init                                                                       */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_ext_init(ehem_ctx *ctx, const char *epk_b64,
                      ehem_ext_init_info **out)
{
    ehem_json *root = NULL;
    ehem_ext_init_info *info;
    char *body;
    ehem_rc rc;

    if (ctx == NULL || epk_b64 == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!is_b64_of_32(epk_b64)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/init: epk must be standard base64 of "
                             "exactly 32 bytes");
    }

    body = epk_body(epk_b64);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/auth/ext/init",
                                 body, EXT_PAIR_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = req_str(ctx, root, "auth/ext/init", "request", &info->request);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "auth/ext/init", "eid", &info->eid);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_ext_init_free(info);
        return rc;
    }
    *out = info;
    return EHEM_OK;
}

void ehem_ext_init_free(ehem_ext_init_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->request);
    free(info->eid);
    free(info);
}

/* -------------------------------------------------------------------------- */
/* validate                                                                   */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_ext_validate(ehem_ctx *ctx, const char *pid_b64,
                          const char *reply_jwt, ehem_ext_validate_info **out)
{
    ehem_json *root = NULL, *obj;
    ehem_ext_validate_info *info;
    char *body = NULL;
    ehem_rc rc;

    if (ctx == NULL || pid_b64 == NULL || reply_jwt == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!is_b64_of_32(pid_b64)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/validate: pid must be standard base64 of "
                             "exactly 32 bytes (the device stores the decoded "
                             "pid as a fixed 32-byte descriptor suffix)");
    }
    if (reply_jwt[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/validate: reply JWT is empty");
    }

    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "pid", pid_b64) &&
        ehem_json_add_string(obj, "reply", reply_jwt)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/auth/ext/validate",
                                 body, EXT_PAIR_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        /* 406 = repo import failed: ExtAuth slot table full (8) OR an
         * identical public key already paired (repo dedup) — the wire does
         * not distinguish them (empty payload); name both in the detail. */
        if (ehem_last_error(ctx)->http_status == 406) {
            return ehem_ctx_fail(ctx, EHEM_ERR_DEVICE, 406, NULL,
                                 "auth/ext/validate: import refused (406) — "
                                 "ExtAuth slots full (max 8) or this "
                                 "authenticator key is already paired");
        }
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = req_str(ctx, root, "auth/ext/validate", "kid", &info->kid);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "auth/ext/validate", "code", &info->code);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_ext_validate_free(info);
        return rc;
    }
    *out = info;
    return EHEM_OK;
}

void ehem_ext_validate_free(ehem_ext_validate_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->kid);
    free(info->code);
    free(info);
}

/* -------------------------------------------------------------------------- */
/* mac                                                                        */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_ext_mac(ehem_ctx *ctx, const char *epk_b64,
                     ehem_ext_mac_info **out)
{
    ehem_json *root = NULL;
    ehem_ext_mac_info *info;
    char *body;
    ehem_rc rc;

    if (ctx == NULL || epk_b64 == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!is_b64_of_32(epk_b64)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/mac: epk must be standard base64 of "
                             "exactly 32 bytes");
    }

    body = epk_body(epk_b64);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/auth/ext/mac",
                                 body, EXT_PAIR_SCOPE, EHEM_TLS_REQ_DEFAULT,
                                 &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = req_str(ctx, root, "auth/ext/mac", "nonce", &info->nonce);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "auth/ext/mac", "mac", &info->mac);
    }
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "auth/ext/mac", "eid", &info->eid);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_ext_mac_free(info);
        return rc;
    }
    *out = info;
    return EHEM_OK;
}

void ehem_ext_mac_free(ehem_ext_mac_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->nonce);
    free(info->mac);
    free(info->eid);
    free(info);
}
