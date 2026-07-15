/*
 * proto_common.c — shared request path for protocol bindings, including the
 * automatic expired-certificate recovery.
 *
 * implements: REQ-NET-005, REQ-API-003 (HTTP→rc mapping)
 */
#include "proto_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* One transport send with the request assembled from ctx + args. */
static ehem_rc do_send(ehem_ctx *ctx, const ehem_transport *t,
                       ehem_http_method method, const char *path,
                       const char *json_body,
                       ehem_tls_req_override tls_override,
                       int fresh_connection, ehem_response *resp)
{
    ehem_header headers[2];
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
    return ehem_transport_send(t, &req, resp);
}

ehem_rc ehem_proto_request_raw(ehem_ctx *ctx, ehem_http_method method,
                               const char *path, const char *json_body,
                               ehem_tls_req_override tls_override,
                               char **body_out)
{
    const ehem_transport *t = ehem_ctx_transport(ctx);
    ehem_response resp;
    ehem_rc rc;

    if (t == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL, "no transport configured");
    }

    rc = do_send(ctx, t, method, path, json_body, tls_override, 0, &resp);

    /*
     * Automatic certificate recovery (REQ-NET-005): only when the failure is
     * classified as "peer certificate EXPIRED", recovery is enabled, and this
     * request is not itself part of a check-in (recursion guard). The check-in
     * refreshes the device certificate; the original request is then retried
     * once on a fresh connection under the normal TLS posture.
     */
    if (rc != EHEM_OK && ehem_transport_last_tls_expired(t) &&
        !ctx->no_auto_checkin && !ctx->in_checkin &&
        tls_override == EHEM_TLS_REQ_DEFAULT) {
        /* The check-in reuses the transport, overwriting its last-error state:
         * keep the original failure detail for reporting. */
        char orig_detail[256];
        ehem_rc orig_rc = rc;
        ehem_rc crc;

        snprintf(orig_detail, sizeof orig_detail, "%s",
                 ehem_transport_last_detail(t));

        crc = ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL);
        if (crc != EHEM_OK) {
            /* Recovery failed: report the ORIGINAL error, noting the attempt.
             * ehem_checkin_run already recorded its own detail; fold it in. */
            return ehem_ctx_fail(ctx, orig_rc, 0, NULL,
                                 "%s: %s (automatic check-in recovery failed: %s)",
                                 path, orig_detail, ehem_last_error(ctx)->message);
        }
        rc = do_send(ctx, t, method, path, json_body, tls_override,
                     /*fresh_connection=*/1, &resp);
        if (rc == EHEM_OK) {
            /* The retry verified against the device: the refresh took effect. */
            ctx->cert_refreshed = true;
        } else if (ehem_transport_last_tls_expired(t)) {
            /* Live-device finding (2026-07-15): the device can accept a
             * certificate update (checkin status OK, newcrt present) yet keep
             * serving the old certificate until its TLS server restarts. Say
             * so instead of repeating the raw TLS error. */
            return ehem_ctx_fail(ctx, orig_rc, 0, NULL,
                                 "%s: %s (check-in completed and the device "
                                 "accepted a certificate update, but it still "
                                 "serves the old certificate — a device reboot "
                                 "may be required to apply it)",
                                 path, orig_detail);
        }
    }

    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, rc, 0, NULL, "%s: %s",
                             path, ehem_transport_last_detail(t));
    }

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
                                ehem_tls_req_override tls_override,
                                ehem_json **root_out)
{
    char *body = NULL;
    ehem_json *root;
    ehem_rc rc;

    rc = ehem_proto_request_raw(ctx, method, path, json_body, tls_override, &body);
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
