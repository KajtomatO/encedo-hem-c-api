/*
 * proto_notify.c — client for the Encedo notification broker
 * (the api.encedo.com/notify family): the cloud legs of the ExtAuth
 * pairing and push-confirm login flows.
 *
 * implements: REQ-AUTH-008, REQ-API-005
 *
 * The broker API has NO documentation — shapes reconstructed from the
 * Encedo Manager and hem-api-tester (test_5/test_6) and pinned by live
 * probes. Design constraints (REQ-AUTH-008):
 *   - every call unauthenticated and ALWAYS fully TLS-verified
 *     (EHEM_TLS_REQ_VERIFY), independent of the device-URL trust mode;
 *   - HTTP 202 on the polling endpoints is a RESULT ("still pending"),
 *     not an error — so this file drives the transport directly instead
 *     of the device request path (which folds all statuses into rc);
 *   - no pacing, no cert recovery, no bearer — none apply to the cloud;
 *   - broker error payloads are preserved in ehem_last_error.
 */
#include "ehem/auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "json.h"
#include "proto_common.h"
#include "transport.h"

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* Copy REQUIRED string field `key` into *dst (PROTOCOL/NOMEM on failure). */
static ehem_rc req_str(ehem_ctx *ctx, const ehem_json *root, const char *what,
                       const char *key, char **dst)
{
    const char *s;
    if (!ehem_json_get_string(root, key, &s)) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "%s: broker response missing '%s'", what, key);
    }
    *dst = dup_str(s);
    if (*dst == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    return EHEM_OK;
}

/*
 * One broker exchange. `subpath` is appended to the base URL ("/session",
 * "/register/check/<rid>", …). On EHEM_OK, *status_out is the HTTP status
 * (2xx only — anything else already failed with the payload preserved) and
 * *body_out the response body (may be NULL for a body-less 202; caller
 * frees). Non-2xx and transport failures are recorded and mapped here.
 */
static ehem_rc notify_send(ehem_ctx *ctx, ehem_http_method method,
                           const char *notify_url, const char *subpath,
                           const char *json_body,
                           long *status_out, char **body_out)
{
    const ehem_transport *t = ehem_ctx_transport(ctx);
    const char *base;
    char *url;
    ehem_header headers[2];
    size_t header_count = 0;
    ehem_request req;
    ehem_response resp;
    ehem_rc rc;

    *status_out = 0;
    *body_out = NULL;
    if (t == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "no transport configured");
    }

    base = (notify_url != NULL) ? notify_url : EHEM_DEFAULT_NOTIFY_URL;
    if (base[0] == '\0') {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "notify: empty broker URL");
    }
    url = malloc(strlen(base) + strlen(subpath) + 1);
    if (url == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    sprintf(url, "%s%s", base, subpath);

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
    req.path               = url;
    req.headers            = headers;
    req.header_count       = header_count;
    req.body               = (const uint8_t *)json_body;
    req.body_len           = (json_body != NULL) ? strlen(json_body) : 0;
    req.connect_timeout_ms = ctx->connect_timeout_ms;
    req.total_timeout_ms   = ctx->total_timeout_ms;
    req.tls_override       = EHEM_TLS_REQ_VERIFY;   /* always, cloud leg */

    memset(&resp, 0, sizeof resp);
    rc = ehem_transport_send(t, &req, &resp);
    if (rc != EHEM_OK) {
        ehem_rc frc = ehem_ctx_fail(ctx, rc, 0, NULL, "%s: %s", url,
                                    ehem_transport_last_detail(t));
        free(url);
        return frc;
    }

    if (resp.status < 200 || resp.status >= 300) {
        rc = ehem_ctx_fail(ctx, ehem_proto_map_http_status(resp.status),
                           resp.status, (const char *)resp.body,
                           "%s: HTTP %ld", url, resp.status);
        free(url);
        ehem_response_free(&resp);
        return rc;
    }
    free(url);

    *status_out = resp.status;
    *body_out = (char *)resp.body;   /* may be NULL (body-less 202) */
    resp.body = NULL;
    resp.body_len = 0;
    ehem_response_free(&resp);
    return EHEM_OK;
}

/* notify_send + JSON parse for calls that require a 200 object body. */
static ehem_rc notify_send_json(ehem_ctx *ctx, ehem_http_method method,
                                const char *notify_url, const char *subpath,
                                const char *json_body, ehem_json **root_out)
{
    long status = 0;
    char *body = NULL;
    ehem_rc rc = notify_send(ctx, method, notify_url, subpath, json_body,
                             &status, &body);
    if (rc != EHEM_OK) {
        return rc;
    }
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, status, NULL,
                             "%s: empty broker response body", subpath);
    }
    *root_out = ehem_json_parse(body, strlen(body));
    if (*root_out == NULL || !ehem_json_is_object(*root_out)) {
        ehem_rc frc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, status, body,
                                    "%s: broker response is not a JSON object",
                                    subpath);
        ehem_json_free(*root_out);
        *root_out = NULL;
        free(body);
        return frc;
    }
    free(body);
    return EHEM_OK;
}

/* -------------------------------------------------------------------------- */
/* session                                                                    */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_notify_session(ehem_ctx *ctx, const char *notify_url,
                            const char *eid_b64, char **epk_out)
{
    ehem_json *root = NULL, *obj;
    char *body = NULL;
    ehem_rc rc;

    if (ctx == NULL || epk_out == NULL) {
        return EHEM_ERR_ARG;
    }
    *epk_out = NULL;
    ehem_ctx_clear_error(ctx);

    if (eid_b64 != NULL) {
        obj = ehem_json_new_object();
        if (obj == NULL || !ehem_json_add_string(obj, "eid", eid_b64) ||
            (body = ehem_json_print(obj)) == NULL) {
            ehem_json_free(obj);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL,
                                 "out of memory");
        }
        ehem_json_free(obj);
    }
    rc = notify_send_json(ctx, body != NULL ? EHEM_HTTP_POST : EHEM_HTTP_GET,
                          notify_url, "/session", body, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = req_str(ctx, root, "notify/session", "epk", epk_out);
    ehem_json_free(root);
    return rc;
}

/* -------------------------------------------------------------------------- */
/* registration triple                                                        */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_notify_register_init(ehem_ctx *ctx, const char *notify_url,
                                  const char *epk_b64, const char *eid_b64,
                                  const char *request_jwt,
                                  ehem_notify_register_info **out)
{
    ehem_json *root = NULL, *obj;
    ehem_notify_register_info *info;
    char *body = NULL;
    ehem_rc rc;

    if (ctx == NULL || epk_b64 == NULL || eid_b64 == NULL ||
        request_jwt == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "epk", epk_b64) &&
        ehem_json_add_string(obj, "eid", eid_b64) &&
        ehem_json_add_string(obj, "request", request_jwt)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = notify_send_json(ctx, EHEM_HTTP_POST, notify_url, "/register/init",
                          body, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        ehem_json_free(root);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    rc = req_str(ctx, root, "notify/register/init", "rid", &info->rid);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "notify/register/init", "link", &info->link);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_notify_register_info_free(info);
        return rc;
    }
    *out = info;
    return EHEM_OK;
}

void ehem_notify_register_info_free(ehem_notify_register_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->rid);
    free(info->link);
    free(info);
}

ehem_rc ehem_notify_register_check(ehem_ctx *ctx, const char *notify_url,
                                   const char *rid,
                                   ehem_notify_pairing_reply **out)
{
    ehem_notify_pairing_reply *r;
    char subpath[160], *body = NULL;
    long status = 0;
    int m;
    ehem_rc rc;

    if (ctx == NULL || rid == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    m = snprintf(subpath, sizeof subpath, "/register/check/%s", rid);
    if (m <= 0 || (size_t)m >= sizeof subpath) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "notify/register/check: rid too long");
    }

    rc = notify_send(ctx, EHEM_HTTP_GET, notify_url, subpath, NULL,
                     &status, &body);
    if (rc != EHEM_OK) {
        return rc;
    }

    r = calloc(1, sizeof *r);
    if (r == NULL) {
        free(body);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    if (status == 202) {
        free(body);
        r->pending = 1;
        *out = r;
        return EHEM_OK;
    }

    /* 200: the phone completed its side — {pid, reply}. */
    ehem_json *root = (body != NULL) ? ehem_json_parse(body, strlen(body))
                                     : NULL;
    if (root == NULL || !ehem_json_is_object(root)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, status, body,
                           "notify/register/check: broker response is not a "
                           "JSON object");
        ehem_json_free(root);
        free(body);
        free(r);
        return rc;
    }
    free(body);
    rc = req_str(ctx, root, "notify/register/check", "pid", &r->pid);
    if (rc == EHEM_OK) {
        rc = req_str(ctx, root, "notify/register/check", "reply", &r->reply);
    }
    ehem_json_free(root);
    if (rc != EHEM_OK) {
        ehem_notify_pairing_reply_free(r);
        return rc;
    }
    *out = r;
    return EHEM_OK;
}

void ehem_notify_pairing_reply_free(ehem_notify_pairing_reply *r)
{
    if (r == NULL) {
        return;
    }
    free(r->pid);
    free(r->reply);
    free(r);
}

ehem_rc ehem_notify_register_finalise(ehem_ctx *ctx, const char *notify_url,
                                      const char *rid, const char *kid_hex,
                                      const char *code_b64)
{
    ehem_json *obj;
    char subpath[160], *body = NULL, *resp_body = NULL;
    long status = 0;
    int m;
    ehem_rc rc;

    if (ctx == NULL || rid == NULL || kid_hex == NULL || code_b64 == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    m = snprintf(subpath, sizeof subpath, "/register/finalise/%s", rid);
    if (m <= 0 || (size_t)m >= sizeof subpath) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "notify/register/finalise: rid too long");
    }

    /* The device's /ext/validate result, passed through verbatim. */
    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "kid", kid_hex) &&
        ehem_json_add_string(obj, "code", code_b64)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = notify_send(ctx, EHEM_HTTP_POST, notify_url, subpath, body,
                     &status, &resp_body);
    ehem_json_string_free(body);
    free(resp_body);   /* 200 is the contract; the body carries nothing */
    return rc;
}

/* -------------------------------------------------------------------------- */
/* event pair                                                                 */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_notify_event_new(ehem_ctx *ctx, const char *notify_url,
                              const char *authreq_jwt, const char *epk_b64,
                              char **eventid_out)
{
    ehem_json *root = NULL, *obj;
    char *body = NULL;
    ehem_rc rc;

    if (ctx == NULL || authreq_jwt == NULL || epk_b64 == NULL ||
        eventid_out == NULL) {
        return EHEM_ERR_ARG;
    }
    *eventid_out = NULL;
    ehem_ctx_clear_error(ctx);

    /* The device's /ext/request result, passed through verbatim. */
    obj = ehem_json_new_object();
    if (obj != NULL && ehem_json_add_string(obj, "authreq", authreq_jwt) &&
        ehem_json_add_string(obj, "epk", epk_b64)) {
        body = ehem_json_print(obj);
    }
    ehem_json_free(obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = notify_send_json(ctx, EHEM_HTTP_POST, notify_url, "/event/new",
                          body, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = req_str(ctx, root, "notify/event/new", "eventid", eventid_out);
    ehem_json_free(root);
    return rc;
}

ehem_rc ehem_notify_event_check(ehem_ctx *ctx, const char *notify_url,
                                const char *eventid,
                                ehem_notify_event_result **out)
{
    ehem_notify_event_result *r;
    char subpath[160], *body = NULL;
    long status = 0;
    int m;
    ehem_rc rc;

    if (ctx == NULL || eventid == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    m = snprintf(subpath, sizeof subpath, "/event/check/%s", eventid);
    if (m <= 0 || (size_t)m >= sizeof subpath) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "notify/event/check: eventid too long");
    }

    rc = notify_send(ctx, EHEM_HTTP_GET, notify_url, subpath, NULL,
                     &status, &body);
    if (rc != EHEM_OK) {
        return rc;
    }

    r = calloc(1, sizeof *r);
    if (r == NULL) {
        free(body);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    if (status == 202) {
        free(body);
        r->pending = 1;
        *out = r;
        return EHEM_OK;
    }

    /* 200: denied (a `deny` field, any type/value — the app sends no
     * authreply on deny) or approved ({authreply}). */
    ehem_json *root = (body != NULL) ? ehem_json_parse(body, strlen(body))
                                     : NULL;
    if (root == NULL || !ehem_json_is_object(root)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, status, body,
                           "notify/event/check: broker response is not a "
                           "JSON object");
        ehem_json_free(root);
        free(body);
        free(r);
        return rc;
    }

    if (ehem_json_has(root, "deny")) {
        r->denied = 1;
    } else {
        const char *s = NULL;
        if (!ehem_json_get_string(root, "authreply", &s)) {
            rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, status, body,
                               "notify/event/check: 200 with neither "
                               "'authreply' nor 'deny' — unknown broker shape");
            ehem_json_free(root);
            free(body);
            free(r);
            return rc;
        }
        r->authreply = dup_str(s);
        if (r->authreply == NULL) {
            ehem_json_free(root);
            free(body);
            free(r);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL,
                                 "out of memory");
        }
    }
    ehem_json_free(root);
    free(body);
    *out = r;
    return EHEM_OK;
}

void ehem_notify_event_result_free(ehem_notify_event_result *r)
{
    if (r == NULL) {
        return;
    }
    free(r->authreply);
    free(r);
}

void ehem_notify_string_free(char *s)
{
    free(s);
}
