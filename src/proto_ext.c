/*
 * proto_ext.c — bindings for the ExtAuth (auth/ext) API group: the pairing
 * trio init / validate / mac, the unauthenticated login pair request /
 * token, and the confirm engine composing them with the broker client.
 *
 * implements: REQ-AUTH-006, REQ-AUTH-007, REQ-AUTH-009, REQ-API-005
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "crypto_shim.h"   /* ehem_zeroize — bearers are sensitive material */
#include "ejwt.h"          /* standard-base64 decoder for arg validation */
#include "json.h"
#include "proto_auth.h"    /* ehem_auth_cache_seed — confirm engine */
#include "proto_common.h"
#include "proto_ext.h"     /* poll-interval test seam */
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

/* -------------------------------------------------------------------------- */
/* login pair: request / token (REQ-AUTH-007)                                 */
/* -------------------------------------------------------------------------- */

/*
 * POST an unauthenticated ext-login request with the RTC-403 recovery: the
 * firmware checks "RTC set" before anything else on these endpoints and 403s
 * a device whose clock is unset (fresh boot — KNOWN-ISSUES). Mirror the
 * fetch_challenge pattern (proto_auth.c, REQ-AUTH-004): at most ONE recovery
 * check-in + one retry per binding call, honoring no_auto_checkin and the
 * in_checkin recursion guard.
 */
static ehem_rc ext_login_post(ehem_ctx *ctx, const char *path,
                              const char *body, ehem_json **root_out)
{
    ehem_rc rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, path, body,
                                         NULL, EHEM_TLS_REQ_DEFAULT, root_out);
    if (rc == EHEM_OK) {
        return EHEM_OK;
    }
    if (ehem_last_error(ctx)->http_status == 403 &&
        !ctx->no_auto_checkin && !ctx->in_checkin) {
        /* Device legs run relaxed: an RTC-broken device's view of its own
         * certificate validity is unreliable (same posture as the login
         * challenge recovery). */
        ehem_rc crc = ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL);
        if (crc != EHEM_OK) {
            /* Copy the check-in's message out before ehem_ctx_fail overwrites
             * the shared last-error buffer it lives in (no aliasing). */
            char checkin_detail[EHEM_ERR_MSG_MAX];
            snprintf(checkin_detail, sizeof checkin_detail, "%s",
                     ehem_last_error(ctx)->message);
            return ehem_ctx_fail(ctx, rc, 403, NULL,
                                 "%s: HTTP 403 (device RTC not set; automatic "
                                 "check-in failed: %s)", path, checkin_detail);
        }
        rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, path, body,
                                     NULL, EHEM_TLS_REQ_DEFAULT, root_out);
    }
    return rc;
}

ehem_rc ehem_ext_request(ehem_ctx *ctx, const char *epk_b64, const char *scope,
                         const char *ctx_str, const char *note,
                         ehem_ext_request_info **out)
{
    ehem_json *root = NULL, *obj;
    ehem_ext_request_info *info;
    char *body = NULL;
    size_t n;
    ehem_rc rc;

    if (ctx == NULL || epk_b64 == NULL || scope == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    if (!is_b64_of_32(epk_b64)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/request: epk must be standard base64 of "
                             "exactly 32 bytes");
    }
    if (strlen(scope) > 1023) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/request: scope exceeds 1023 bytes");
    }
    n = (ctx_str != NULL) ? strlen(ctx_str) : 1;
    if (n < 1 || n > 64) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/request: ctx must be 1..64 chars (the "
                             "firmware silently drops out-of-range values)");
    }
    n = (note != NULL) ? strlen(note) : 1;
    if (n < 1 || n > 128) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/request: note must be 1..128 chars (the "
                             "firmware silently drops out-of-range values)");
    }

    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "epk", epk_b64) &&
        ehem_json_add_string(obj, "scope", scope) &&
        (ctx_str == NULL || ehem_json_add_string(obj, "ctx", ctx_str)) &&
        (note == NULL || ehem_json_add_string(obj, "note", note))) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ext_login_post(ctx, "/api/auth/ext/request", body, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = req_str(ctx, root, "auth/ext/request", "authreq", &info->authreq);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "auth/ext/request", "epk", &info->epk);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_ext_request_free(info);
        return rc;
    }
    *out = info;
    return EHEM_OK;
}

void ehem_ext_request_free(ehem_ext_request_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->authreq);
    free(info->epk);
    free(info);
}

ehem_rc ehem_ext_token(ehem_ctx *ctx, const char *authreply_jwt,
                       char **token_out)
{
    ehem_json *root = NULL, *obj;
    char *body = NULL;
    const char *s;
    ehem_rc rc;

    if (ctx == NULL || authreply_jwt == NULL || token_out == NULL) {
        return EHEM_ERR_ARG;
    }
    *token_out = NULL;
    ehem_ctx_clear_error(ctx);

    if (authreply_jwt[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "ext/token: authreply JWT is empty");
    }

    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "authreply", authreply_jwt)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ext_login_post(ctx, "/api/auth/ext/token", body, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        /* 401 = JWT decode/validate/jti failure (incl. an expired or replayed
         * reply); 406 = the reply is well-signed but not acceptable (unknown
         * authenticator, non-"A" scheme, ciphertext fails decrypt/HMAC). Both
         * are "the authenticator's reply did not authenticate" to a caller. */
        long status = ehem_last_error(ctx)->http_status;
        if (status == 401) {
            return ehem_ctx_fail(ctx, EHEM_ERR_AUTH_FAILED, 401, NULL,
                                 "auth/ext/token: reply JWT rejected (401 — "
                                 "bad signature, expired, or replayed nonce)");
        }
        if (status == 406) {
            return ehem_ctx_fail(ctx, EHEM_ERR_AUTH_FAILED, 406, NULL,
                                 "auth/ext/token: reply not acceptable (406 — "
                                 "unknown authenticator, unsupported scheme, "
                                 "or scope ciphertext failed to authenticate)");
        }
        return rc;
    }

    if (!ehem_json_get_string(root, "token", &s)) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "auth/ext/token: response missing 'token'");
    }
    *token_out = dup_str(s);
    ehem_json_free(root);
    if (*token_out == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    return EHEM_OK;
}

void ehem_ext_token_free(char *token)
{
    free(token);
}

/* -------------------------------------------------------------------------- */
/* confirm engine (REQ-AUTH-009)                                              */
/* -------------------------------------------------------------------------- */

#define CONFIRM_POLL_INTERVAL_MS 5000L   /* the tester's cadence */

static long g_poll_interval_ms = CONFIRM_POLL_INTERVAL_MS;

void ehem_ext_test_set_poll_interval(long ms)
{
    g_poll_interval_ms = (ms > 0) ? ms : CONFIRM_POLL_INTERVAL_MS;
}

struct ehem_ext_confirm {
    char *notify_url;   /* owned copy, or NULL for the default base */
    char *scope;        /* the cache key the bearer will be seeded under */
    char *eventid;
    int   terminal;     /* approved, denied, or token-redemption failed */
};

ehem_rc ehem_ext_confirm_begin(ehem_ctx *ctx, const char *notify_url,
                               const char *scope, const char *ctx_str,
                               const char *note, ehem_ext_confirm **out)
{
    ehem_ext_confirm *c;
    ehem_ext_request_info *reqi = NULL;
    char *epk = NULL;
    ehem_rc rc;

    if (ctx == NULL || scope == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;

    c = calloc(1, sizeof *c);
    if (c == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    c->scope = dup_str(scope);
    if (c->scope == NULL ||
        (notify_url != NULL && (c->notify_url = dup_str(notify_url)) == NULL)) {
        ehem_ext_confirm_cancel(c);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* session (credential-free GET) → device authreq → broker push. */
    rc = ehem_notify_session(ctx, c->notify_url, NULL, &epk);
    if (rc == EHEM_OK) {
        rc = ehem_ext_request(ctx, epk, scope, ctx_str, note, &reqi);
    }
    if (rc == EHEM_OK) {
        rc = ehem_notify_event_new(ctx, c->notify_url, reqi->authreq,
                                   reqi->epk, &c->eventid);
    }
    ehem_ext_request_free(reqi);
    ehem_notify_string_free(epk);
    if (rc != EHEM_OK) {
        ehem_ext_confirm_cancel(c);
        return rc;
    }
    *out = c;
    return EHEM_OK;
}

ehem_rc ehem_ext_confirm_poll(ehem_ctx *ctx, ehem_ext_confirm *c,
                              ehem_confirm_status *status_out)
{
    ehem_notify_event_result *r = NULL;
    char *token = NULL;
    ehem_rc rc;

    if (ctx == NULL || c == NULL || status_out == NULL) {
        return EHEM_ERR_ARG;
    }
    *status_out = EHEM_CONFIRM_PENDING;
    if (c->terminal) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "confirm: handle already finished (single-use)");
    }

    rc = ehem_notify_event_check(ctx, c->notify_url, c->eventid, &r);
    if (rc != EHEM_OK) {
        return rc;   /* transient: NOT terminal, the caller may poll again */
    }

    if (r->pending) {
        ehem_notify_event_result_free(r);
        return EHEM_OK;
    }

    if (r->denied) {
        ehem_notify_event_result_free(r);
        c->terminal = 1;
        return ehem_ctx_fail(ctx, EHEM_ERR_USER_REJECTED, 0, NULL,
                             "confirm: rejected on the authenticator");
    }

    /* Approved: redeem the authreply and seed the ordinary token cache under
     * the scope the caller asked to confirm (the pre-rewrite string — the
     * cache key bindings look up). */
    rc = ehem_ext_token(ctx, r->authreply, &token);
    ehem_notify_event_result_free(r);
    if (rc != EHEM_OK) {
        c->terminal = 1;   /* re-redeeming the same reply cannot succeed */
        return rc;
    }
    rc = ehem_auth_cache_seed(ctx, c->scope, token);
    ehem_zeroize(token, strlen(token));
    ehem_ext_token_free(token);
    c->terminal = 1;
    if (rc != EHEM_OK) {
        return rc;
    }
    *status_out = EHEM_CONFIRM_APPROVED;
    return EHEM_OK;
}

ehem_rc ehem_ext_confirm_wait(ehem_ctx *ctx, ehem_ext_confirm *c,
                              long timeout_ms)
{
    long elapsed = 0;

    if (ctx == NULL || c == NULL || timeout_ms <= 0) {
        return EHEM_ERR_ARG;
    }

    for (;;) {
        ehem_confirm_status status = EHEM_CONFIRM_PENDING;
        ehem_rc rc = ehem_ext_confirm_poll(ctx, c, &status);
        if (rc != EHEM_OK) {
            return rc;   /* rejected (terminal) or a poll failure (retryable) */
        }
        if (status == EHEM_CONFIRM_APPROVED) {
            return EHEM_OK;
        }
        if (elapsed >= timeout_ms) {
            /* Deliberately NOT terminal: the caller may resume waiting. */
            return ehem_ctx_fail(ctx, EHEM_ERR_CONFIRM_TIMEOUT, 0, NULL,
                                 "confirm: not answered within %ld ms",
                                 timeout_ms);
        }
        {
            long step = g_poll_interval_ms;
            if (step > timeout_ms - elapsed) {
                step = timeout_ms - elapsed;
            }
            ehem_proto_sleep_ms(step);
            elapsed += step;
        }
    }
}

void ehem_ext_confirm_cancel(ehem_ext_confirm *c)
{
    if (c == NULL) {
        return;
    }
    free(c->notify_url);
    free(c->scope);
    free(c->eventid);
    free(c);
}
