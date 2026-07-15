/*
 * proto_system.c — bindings for the `system` API group: status and version.
 *
 * implements: REQ-SYS-001, REQ-SYS-002, REQ-API-005
 *
 * The first real protocol bindings and the template the rest follow: build a
 * request → send through the context transport → translate HTTP/transport
 * failure into ehem_rc + last-error detail → parse the JSON body into a
 * caller-owned struct with tolerant parsing (unknown fields ignored, missing
 * required fields → EHEM_ERR_PROTOCOL).
 */
#include "ehem/system.h"

#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "transport.h"
#include "json.h"

/* -------------------------------------------------------------------------- */
/* helpers                                                                    */
/* -------------------------------------------------------------------------- */

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* Copy an optional string field into *dst if present. Returns false only on
 * allocation failure; an absent field is success (leaves *dst NULL). */
static bool opt_str(const ehem_json *obj, const char *key, char **dst)
{
    const char *s;
    if (!ehem_json_get_string(obj, key, &s)) {
        return true;   /* absent — fine */
    }
    *dst = dup_str(s);
    return (*dst != NULL);
}

/* Map a non-2xx HTTP status to an ehem_rc (REQ-API-003). Refined as auth lands
 * in later milestones; for M1 it covers the unauthenticated system endpoints. */
static ehem_rc map_http_status(long status)
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

/*
 * GET `path` and hand back the parsed JSON object in *root_out (caller frees
 * with ehem_json_free). On any failure, records last-error detail on ctx and
 * returns the mapped rc; *root_out is untouched.
 */
static ehem_rc get_json_object(ehem_ctx *ctx, const char *path, ehem_json **root_out)
{
    const ehem_transport *t = ehem_ctx_transport(ctx);
    ehem_header hdr = { "Accept", "application/json" };
    ehem_request req;
    ehem_response resp;
    ehem_json *root;
    ehem_rc rc;

    if (t == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL, "no transport configured");
    }

    memset(&req, 0, sizeof req);
    req.method             = EHEM_HTTP_GET;
    req.path               = path;
    req.headers            = &hdr;
    req.header_count       = 1;
    req.connect_timeout_ms = ctx->connect_timeout_ms;
    req.total_timeout_ms   = ctx->total_timeout_ms;

    memset(&resp, 0, sizeof resp);
    rc = ehem_transport_send(t, &req, &resp);
    if (rc != EHEM_OK) {
        /* Transport-level failure (unreachable / network): surface curl's text. */
        return ehem_ctx_fail(ctx, rc, 0, NULL, "%s: %s",
                             path, ehem_transport_last_detail(t));
    }

    if (resp.status < 200 || resp.status >= 300) {
        rc = ehem_ctx_fail(ctx, map_http_status(resp.status), resp.status,
                           (const char *)resp.body,
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

    root = ehem_json_parse((const char *)resp.body, resp.body_len);
    if (root == NULL || !ehem_json_is_object(root)) {
        rc = ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, resp.status,
                           (const char *)resp.body,
                           "%s: %s", path,
                           (root == NULL) ? "malformed JSON response"
                                          : "response is not a JSON object");
        ehem_json_free(root);   /* NULL-safe */
        ehem_response_free(&resp);
        return rc;
    }

    ehem_response_free(&resp);
    *root_out = root;
    return EHEM_OK;
}

/* -------------------------------------------------------------------------- */
/* status                                                                     */
/* -------------------------------------------------------------------------- */

static ehem_rc parse_status(ehem_ctx *ctx, const ehem_json *root,
                            ehem_status_info **out)
{
    ehem_status_info *s = calloc(1, sizeof *s);
    const ehem_json *node;
    const char *missing = NULL;

    if (s == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Required scalars (always present per the doc). */
    if (!ehem_json_get_int64(root, "ctx", &s->ctx)) {
        missing = "ctx";
    } else if (!ehem_json_get_int64(root, "fls_state", &s->fls_state)) {
        missing = "fls_state";
    } else if (!ehem_json_get_int64(root, "uptime", &s->uptime)) {
        missing = "uptime";
    } else if (!ehem_json_get_double(root, "temp", &s->temp)) {
        missing = "temp";
    }
    if (missing != NULL) {
        ehem_system_status_free(s);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "system/status: missing required field '%s'", missing);
    }

    /* Optional strings. */
    if (!opt_str(root, "ts", &s->ts) ||
        !opt_str(root, "hostname", &s->hostname) ||
        !opt_str(root, "format", &s->format)) {
        goto oom;
    }

    /* Optional number / booleans (has_* distinguishes 0/false from absent). */
    if (ehem_json_get_int64(root, "time", &s->time))          { s->has_time = true; }
    if (ehem_json_get_bool(root, "fw_upgrade", &s->fw_upgrade)) { s->has_fw_upgrade = true; }
    if (ehem_json_get_bool(root, "inited", &s->inited))        { s->has_inited = true; }
    if (ehem_json_get_bool(root, "https", &s->https))          { s->has_https = true; }
    if (ehem_json_get_bool(root, "tts", &s->tts))              { s->has_tts = true; }

    /* storage: array of per-disk status strings (non-string elements skipped). */
    node = ehem_json_get(root, "storage");
    if (ehem_json_is_array(node)) {
        size_t n = ehem_json_array_size(node);
        if (n > 0) {
            size_t i;
            s->storage = calloc(n, sizeof *s->storage);
            if (s->storage == NULL) {
                goto oom;
            }
            for (i = 0; i < n; i++) {
                const char *v;
                if (ehem_json_as_string(ehem_json_array_get(node, i), &v)) {
                    s->storage[s->storage_count] = dup_str(v);
                    if (s->storage[s->storage_count] == NULL) {
                        goto oom;
                    }
                    s->storage_count++;
                }
            }
        }
    }

    /* repo_stats: nested object, present only with auth. */
    node = ehem_json_get(root, "repo_stats");
    if (ehem_json_is_object(node)) {
        s->has_repo_stats = true;
        ehem_json_get_int64(node, "deleted", &s->repo_stats.deleted);
        ehem_json_get_int64(node, "fragmentation", &s->repo_stats.fragmentation);
        ehem_json_get_int64(node, "freespace", &s->repo_stats.freespace);
        ehem_json_get_int64(node, "invalid", &s->repo_stats.invalid);
        ehem_json_get_int64(node, "total", &s->repo_stats.total);
    }

    *out = s;
    return EHEM_OK;

oom:
    ehem_system_status_free(s);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_system_status(ehem_ctx *ctx, ehem_status_info **out)
{
    ehem_json *root = NULL;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = get_json_object(ctx, "/api/system/status", &root);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = parse_status(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

void ehem_system_status_free(ehem_status_info *status)
{
    size_t i;
    if (status == NULL) {
        return;
    }
    for (i = 0; i < status->storage_count; i++) {
        free(status->storage[i]);
    }
    free(status->storage);
    free(status->ts);
    free(status->hostname);
    free(status->format);
    free(status);
}

/* -------------------------------------------------------------------------- */
/* version                                                                    */
/* -------------------------------------------------------------------------- */

static ehem_rc parse_version(ehem_ctx *ctx, const ehem_json *root,
                             ehem_version_info **out)
{
    ehem_version_info *v = calloc(1, sizeof *v);
    const char *str;
    const char *missing = NULL;

    if (v == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Required strings. */
    if (!ehem_json_get_string(root, "hwv", &str)) {
        missing = "hwv";
    } else if ((v->hwv = dup_str(str)) == NULL) {
        goto oom;
    } else if (!ehem_json_get_string(root, "fwv", &str)) {
        missing = "fwv";
    } else if ((v->fwv = dup_str(str)) == NULL) {
        goto oom;
    } else if (!ehem_json_get_string(root, "blv", &str)) {
        missing = "blv";
    } else if ((v->blv = dup_str(str)) == NULL) {
        goto oom;
    }
    if (missing != NULL) {
        ehem_system_version_free(v);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "system/version: missing required field '%s'", missing);
    }

    /* Optional strings. */
    if (!opt_str(root, "fwk", &v->fwk) ||
        !opt_str(root, "fws", &v->fws) ||
        !opt_str(root, "blk", &v->blk) ||
        !opt_str(root, "bls", &v->bls) ||
        !opt_str(root, "uis", &v->uis) ||
        !opt_str(root, "sd_csd", &v->sd_csd) ||
        !opt_str(root, "sd_cid", &v->sd_cid)) {
        goto oom;
    }

    *out = v;
    return EHEM_OK;

oom:
    ehem_system_version_free(v);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_system_version(ehem_ctx *ctx, ehem_version_info **out)
{
    ehem_json *root = NULL;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = get_json_object(ctx, "/api/system/version", &root);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = parse_version(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

void ehem_system_version_free(ehem_version_info *version)
{
    if (version == NULL) {
        return;
    }
    free(version->hwv);
    free(version->fwv);
    free(version->blv);
    free(version->fwk);
    free(version->fws);
    free(version->blk);
    free(version->bls);
    free(version->uis);
    free(version->sd_csd);
    free(version->sd_cid);
    free(version);
}
