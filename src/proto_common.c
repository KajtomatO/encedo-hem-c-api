/*
 * proto_common.c — shared request path for protocol bindings: bearer injection
 * (REQ-AUTH-003), the automatic expired-certificate recovery (REQ-NET-005), the
 * HTTP→rc mapping (REQ-API-003), and the optional client-side request pace
 * (REQ-NET-006).
 */
#define _POSIX_C_SOURCE 199309L   /* nanosleep / struct timespec under -std=c99 */

#include "proto_common.h"
#include "proto_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>            /* Sleep() — MinGW has no POSIX nanosleep */
#else
#  include <time.h>               /* nanosleep / struct timespec */
#endif

/* implements: REQ-NET-006 (client-side request pacing) */
static void pace_sleep_ms(long ms)
{
    if (ms <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)(ms / 1000);
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        (void)nanosleep(&ts, NULL);
    }
#endif
}

ehem_rc ehem_proto_map_http_status(long status)
{
    switch (status) {
    case 401: return EHEM_ERR_AUTH_FAILED;
    case 403: return EHEM_ERR_SCOPE_DENIED;
    case 404: return EHEM_ERR_NOT_FOUND;
    default:
        if (status >= 400) {
            return EHEM_ERR_DEVICE;   /* other 4xx/5xx: device-reported error */
        }
        return EHEM_ERR_PROTOCOL;     /* unexpected 1xx/3xx for this API */
    }
}

bool ehem_proto_is_kid_hex(const char *s)
{
    size_t i;
    for (i = 0; i < EHEM_PROTO_KID_HEX_LEN; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return s[EHEM_PROTO_KID_HEX_LEN] == '\0';
}

/* Build an "Authorization: Bearer <token>" header value (heap, caller frees). */
static char *make_bearer(const char *token)
{
    size_t n = strlen(token);
    char *v = malloc(7 + n + 1);   /* "Bearer " is 7 chars */
    if (v != NULL) {
        memcpy(v, "Bearer ", 7);
        memcpy(v + 7, token, n + 1);
    }
    return v;
}

/* One transport send with the request assembled from ctx + args. `bearer`, when
 * non-NULL, is the full "Bearer <token>" Authorization value. */
static ehem_rc do_send(ehem_ctx *ctx, const ehem_transport *t,
                       ehem_http_method method, const char *path,
                       const char *json_body, const char *bearer,
                       ehem_tls_req_override tls_override,
                       int fresh_connection, ehem_response *resp)
{
    ehem_header headers[3];
    size_t header_count = 0;
    ehem_request req;

    headers[header_count].name  = "Accept";
    headers[header_count].value = "application/json";
    header_count++;
    if (json_body != NULL) {
        headers[header_count].name  = "Content-Type";
        headers[header_count].value = "application/json";
        header_count++;
    }
    if (bearer != NULL) {
        headers[header_count].name  = "Authorization";
        headers[header_count].value = bearer;
        header_count++;
    }

    memset(&req, 0, sizeof req);
    req.method             = method;
    req.path               = path;
    req.headers            = headers;
    req.header_count       = header_count;
    req.body               = (const uint8_t *)json_body;
    req.body_len           = (json_body != NULL) ? strlen(json_body) : 0;
    req.connect_timeout_ms = ctx->connect_timeout_ms;
    req.total_timeout_ms   = ctx->total_timeout_ms;
    req.tls_override       = tls_override;
    req.fresh_connection   = fresh_connection;

    memset(resp, 0, sizeof *resp);
    /* Client-side pacing (REQ-NET-006): throttle back-to-back requests for
     * rate-sensitive devices. Applies to every dispatch, retries included. */
    pace_sleep_ms(ctx->request_pace_ms);
    return ehem_transport_send(t, &req, resp);
}

ehem_rc ehem_proto_request_raw(ehem_ctx *ctx, ehem_http_method method,
                               const char *path, const char *json_body,
                               const char *scope,
                               ehem_tls_req_override tls_override,
                               char **body_out)
{
    const ehem_transport *t = ehem_ctx_transport(ctx);
    ehem_response resp;
    char *bearer = NULL;
    ehem_rc rc;

    if (t == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL, "no transport configured");
    }

    /* Authenticated request (REQ-AUTH-003): acquire a bearer for the declared
     * scope up front so it rides on every attempt (incl. a cert-recovery
     * resend). A NULL scope is an unauthenticated binding / the login itself. */
    if (scope != NULL) {
        const char *token = NULL;
        rc = ehem_auth_ensure_token(ctx, scope, &token);
        if (rc != EHEM_OK) {
            return rc;   /* auth layer already recorded last-error */
        }
        bearer = make_bearer(token);
        if (bearer == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
    }

    rc = do_send(ctx, t, method, path, json_body, bearer, tls_override, 0, &resp);

    /*
     * Automatic certificate recovery (REQ-NET-005): only when the failure is
     * classified as "peer certificate EXPIRED", recovery is enabled, and this
     * request is not itself part of a check-in (recursion guard). The check-in
     * refreshes the device certificate; the original request is then retried
     * once on a fresh connection under the normal TLS posture (carrying the
     * same bearer).
     */
    if (rc != EHEM_OK && ehem_transport_last_tls_expired(t) &&
        !ctx->no_auto_checkin && !ctx->in_checkin &&
        tls_override == EHEM_TLS_REQ_DEFAULT) {
        char orig_detail[256];
        ehem_rc orig_rc = rc;
        ehem_rc crc;

        snprintf(orig_detail, sizeof orig_detail, "%s",
                 ehem_transport_last_detail(t));

        crc = ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL);
        if (crc != EHEM_OK) {
            free(bearer);
            return ehem_ctx_fail(ctx, orig_rc, 0, NULL,
                                 "%s: %s (automatic check-in recovery failed: %s)",
                                 path, orig_detail, ehem_last_error(ctx)->message);
        }
        rc = do_send(ctx, t, method, path, json_body, bearer, tls_override,
                     /*fresh_connection=*/1, &resp);
        if (rc == EHEM_OK) {
            /* The retry verified against the device: the refresh took effect. */
            ctx->cert_refreshed = true;
        } else if (ehem_transport_last_tls_expired(t)) {
            free(bearer);
            return ehem_ctx_fail(ctx, orig_rc, 0, NULL,
                                 "%s: %s (check-in completed and the device "
                                 "accepted a certificate update, but it still "
                                 "serves the old certificate — a device reboot "
                                 "may be required to apply it)",
                                 path, orig_detail);
        }
    }

    if (rc != EHEM_OK) {
        free(bearer);
        return ehem_ctx_fail(ctx, rc, 0, NULL, "%s: %s",
                             path, ehem_transport_last_detail(t));
    }

    /*
     * Sanctioned auth retry (REQ-AUTH-003): a 401 on an authenticated request
     * means the device rejected the token we sent (typically invalidated
     * server-side, e.g. after a reboot). Drop the cache entry, re-acquire once,
     * and retry once — fresh connection, and WITHOUT re-running cert recovery,
     * so it composes with (does not multiply) the REQ-NET-005 retry. A second
     * 401 falls through to the mapping below → EHEM_ERR_AUTH_FAILED.
     */
    if (resp.status == 401 && scope != NULL) {
        const char *token = NULL;
        ehem_response_free(&resp);
        free(bearer);
        bearer = NULL;

        ehem_auth_invalidate(ctx, scope);
        rc = ehem_auth_ensure_token(ctx, scope, &token);
        if (rc != EHEM_OK) {
            return rc;
        }
        bearer = make_bearer(token);
        if (bearer == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
        rc = do_send(ctx, t, method, path, json_body, bearer, tls_override,
                     /*fresh_connection=*/1, &resp);
        if (rc != EHEM_OK) {
            free(bearer);
            return ehem_ctx_fail(ctx, rc, 0, NULL, "%s: %s",
                                 path, ehem_transport_last_detail(t));
        }
    }

    free(bearer);

    if (resp.status < 200 || resp.status >= 300) {
        rc = ehem_ctx_fail(ctx, ehem_proto_map_http_status(resp.status),
                           resp.status, (const char *)resp.body,
                           "%s: HTTP %ld", path, resp.status);
        ehem_response_free(&resp);
        return rc;
    }

    if (resp.body == NULL || resp.body_len == 0) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, resp.status, NULL,
                           "%s: empty response body", path);
        ehem_response_free(&resp);
        return rc;
    }

    /* Hand the body to the caller; free the rest of the response. */
    *body_out = (char *)resp.body;
    resp.body = NULL;
    resp.body_len = 0;
    ehem_response_free(&resp);
    return EHEM_OK;
}

ehem_rc ehem_proto_request_json(ehem_ctx *ctx, ehem_http_method method,
                                const char *path, const char *json_body,
                                const char *scope,
                                ehem_tls_req_override tls_override,
                                ehem_json **root_out)
{
    char *body = NULL;
    ehem_json *root;
    ehem_rc rc;

    rc = ehem_proto_request_raw(ctx, method, path, json_body, scope,
                                tls_override, &body);
    if (rc != EHEM_OK) {
        return rc;
    }

    root = ehem_json_parse(body, strlen(body));
    if (root == NULL || !ehem_json_is_object(root)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, body, "%s: %s", path,
                           (root == NULL) ? "malformed JSON response"
                                          : "response is not a JSON object");
        ehem_json_free(root);   /* NULL-safe */
        free(body);
        return rc;
    }

    free(body);
    *root_out = root;
    return EHEM_OK;
}
