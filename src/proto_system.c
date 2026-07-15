/*
 * proto_system.c — bindings for the `system` API group: status, version, and
 * the check-in handshake.
 *
 * implements: REQ-SYS-001, REQ-SYS-002, REQ-SYS-003, REQ-API-005
 *
 * The first real protocol bindings and the template the rest follow: build a
 * request → send through the shared request path (proto_common, which also
 * carries the REQ-NET-005 auto-recovery) → parse the JSON body into a
 * caller-owned struct with tolerant parsing (unknown fields ignored, missing
 * required fields → EHEM_ERR_PROTOCOL).
 */
#include "ehem/system.h"

#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "json.h"
#include "proto_common.h"
#include "transport.h"

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

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, "/api/system/status",
                                 NULL, EHEM_TLS_REQ_DEFAULT, &root);
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

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, "/api/system/version",
                                 NULL, EHEM_TLS_REQ_DEFAULT, &root);
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

/* -------------------------------------------------------------------------- */
/* check-in (REQ-SYS-003)                                                     */
/* -------------------------------------------------------------------------- */

/* Leg-3 response → ehem_checkin_info (all fields optional, tolerant). */
static ehem_rc parse_checkin(ehem_ctx *ctx, const ehem_json *root,
                             ehem_checkin_info **out)
{
    ehem_checkin_info *r = calloc(1, sizeof *r);

    if (r == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    if (!opt_str(root, "status", &r->status) ||
        !opt_str(root, "newcrt", &r->newcrt) ||
        !opt_str(root, "newfws", &r->newfws) ||
        !opt_str(root, "newuis", &r->newuis)) {
        ehem_checkin_result_free(r);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    r->cert_updated = (r->newcrt != NULL && r->newcrt[0] != '\0');

    *out = r;
    return EHEM_OK;
}

ehem_rc ehem_checkin_run(ehem_ctx *ctx, int relax_device_tls,
                         ehem_checkin_info **out)
{
    /* Device legs must work while the device certificate is invalid; the
     * payloads are cloud-signed and validated by the device itself, so
     * relaxing verification here does not extend trust (REQ-SYS-003). */
    ehem_tls_req_override dev_ov =
        relax_device_tls ? EHEM_TLS_REQ_RELAX : EHEM_TLS_REQ_DEFAULT;
    char *challenge = NULL;
    char *verified = NULL;
    ehem_json *root = NULL;
    ehem_rc rc;

    /* Recursion guard: nothing inside the flow may trigger auto-recovery. */
    ctx->in_checkin = true;

    /* Leg 1: fetch the device's check-in challenge. */
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_GET, "/api/system/checkin",
                                NULL, dev_ov, &challenge);
    if (rc != EHEM_OK) {
        goto done;
    }

    /* Leg 2: relay the challenge VERBATIM to the Encedo cloud. Always fully
     * TLS-verified — this response is the trust anchor being delivered. */
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_POST, ctx->checkin_url,
                                challenge, EHEM_TLS_REQ_VERIFY, &verified);
    if (rc != EHEM_OK) {
        goto done;
    }

    /* Leg 3: hand the cloud-verified data VERBATIM back to the device. */
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/system/checkin",
                                 verified, dev_ov, &root);
    if (rc != EHEM_OK) {
        goto done;
    }

    if (out != NULL) {
        rc = parse_checkin(ctx, root, out);
    }

done:
    ctx->in_checkin = false;
    ehem_json_free(root);
    free(challenge);
    free(verified);
    return rc;
}

ehem_rc ehem_system_checkin(ehem_ctx *ctx, ehem_checkin_info **out)
{
    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    /* Explicit check-in exists precisely to repair a stale certificate, so the
     * device legs always run relaxed (matches the reference python client). */
    return ehem_checkin_run(ctx, /*relax_device_tls=*/1, out);
}

void ehem_checkin_result_free(ehem_checkin_info *result)
{
    if (result == NULL) {
        return;
    }
    free(result->status);
    free(result->newcrt);
    free(result->newfws);
    free(result->newuis);
    free(result);
}
