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
#include "crypto_shim.h"     /* ehem_cert_parse_leaf, ehem_serial_hex */
#include "ejwt.h"            /* base64 / base64url decoders */
#include "json.h"
#include "proto_auth.h"      /* ehem_auth_invalidate — reboot drops the cache */
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
                                 NULL, NULL, EHEM_TLS_REQ_DEFAULT, &root);
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
                                 NULL, NULL, EHEM_TLS_REQ_DEFAULT, &root);
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

/*
 * Extract a string claim from a JWT carried in a JSON envelope (REQ-SYS-006).
 * `envelope` is a raw check-in leg body — {"check":"<jwt>"} (leg 1) or
 * {"checked":"<jwt>"} (leg 2); `env_key` names the JWT field, `claim` the claim
 * inside the JWT's base64url payload segment. The payload is NOT encrypted and
 * the signature is NOT verified here — the SDK only reads the value (the leg-3
 * relay stays verbatim; the cloud leg was already TLS-verified). On finding the
 * claim, dups its value into *out (caller frees). A missing envelope / segment
 * / claim is tolerated: *out is left untouched and EHEM_OK returned. Returns
 * EHEM_ERR_NOMEM only on allocation failure.
 */
static ehem_rc harvest_jwt_claim(const char *envelope, const char *env_key,
                                 const char *claim, char **out)
{
    ehem_json *env = NULL;
    ehem_json *payload = NULL;
    const char *jwt;
    const char *p1;
    const char *p2;
    const char *value;
    uint8_t *raw = NULL;
    size_t seg_len;
    size_t n;
    ehem_rc rc = EHEM_OK;

    env = ehem_json_parse(envelope, strlen(envelope));
    if (env == NULL) {
        return EHEM_OK;                    /* not JSON — nothing to harvest */
    }
    if (!ehem_json_get_string(env, env_key, &jwt)) {
        goto done;
    }
    p1 = strchr(jwt, '.');                 /* header . payload . signature */
    if (p1 == NULL || (p2 = strchr(p1 + 1, '.')) == NULL) {
        goto done;
    }
    seg_len = (size_t)(p2 - (p1 + 1));
    if (seg_len == 0) {
        goto done;
    }
    raw = malloc(seg_len);                 /* decoded is never larger than input */
    if (raw == NULL) {
        rc = EHEM_ERR_NOMEM;
        goto done;
    }
    n = ehem_b64url_decode(p1 + 1, seg_len, raw, seg_len);
    if (n == (size_t)-1) {
        goto done;                         /* undecodable payload — tolerant */
    }
    payload = ehem_json_parse((const char *)raw, n);
    if (payload != NULL && ehem_json_get_string(payload, claim, &value)) {
        char *dup = dup_str(value);
        if (dup == NULL) {
            rc = EHEM_ERR_NOMEM;
            goto done;
        }
        *out = dup;
    }

done:
    free(raw);
    ehem_json_free(payload);
    ehem_json_free(env);
    return rc;
}

/*
 * Harvest the device's currently-loaded certificate serial from the leg-1
 * `csn` claim (base64 of the raw serial bytes) and store it on the result as
 * normalized uppercase hex, matching ehem_cert_inspect()'s representation so
 * the two can be compared directly. Tolerant: any failure leaves current_serial
 * NULL. Returns EHEM_ERR_NOMEM only on allocation failure.
 */
static ehem_rc harvest_current_serial(const char *challenge,
                                      ehem_checkin_info *r)
{
    char *csn_b64 = NULL;
    uint8_t serial[64];
    char hex[EHEM_CERT_SERIAL_HEX_CAP];
    size_t n;
    ehem_rc rc;

    rc = harvest_jwt_claim(challenge, "check", "csn", &csn_b64);
    if (rc != EHEM_OK || csn_b64 == NULL) {
        return rc;
    }
    n = ehem_b64_std_decode(csn_b64, strlen(csn_b64), serial, sizeof serial);
    free(csn_b64);
    if (n == (size_t)-1 || n == 0) {
        return EHEM_OK;                    /* undecodable serial — tolerant */
    }
    ehem_serial_hex(serial, n, hex, sizeof hex);
    r->current_serial = dup_str(hex);
    return (r->current_serial != NULL) ? EHEM_OK : EHEM_ERR_NOMEM;
}

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

    /* Leg 1: fetch the device's check-in challenge. Unauthenticated (the
     * check-in flow is precisely how a device with no valid session/clock gets
     * bootstrapped), so scope is NULL. */
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_GET, "/api/system/checkin",
                                NULL, NULL, dev_ov, &challenge);
    if (rc != EHEM_OK) {
        goto done;
    }

    /* Leg 2: relay the challenge VERBATIM to the Encedo cloud. Always fully
     * TLS-verified — this response is the trust anchor being delivered. */
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_POST, ctx->checkin_url,
                                challenge, NULL, EHEM_TLS_REQ_VERIFY, &verified);
    if (rc != EHEM_OK) {
        goto done;
    }

    /* Leg 3: hand the cloud-verified data VERBATIM back to the device. */
    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/system/checkin",
                                 verified, NULL, dev_ov, &root);
    if (rc != EHEM_OK) {
        goto done;
    }

    if (out != NULL) {
        rc = parse_checkin(ctx, root, out);
        if (rc == EHEM_OK) {
            /* REQ-SYS-006: expose the cloud-DELIVERED chain (leg-2 `newcrt`)
             * and the device's CURRENT serial (leg-1 `csn`) for cert-install.
             * Harvesting is best-effort and never fails the check-in itself —
             * only an allocation failure propagates. */
            rc = harvest_jwt_claim(verified, "checked", "newcrt",
                                   &(*out)->newcrt_chain);
            if (rc == EHEM_OK) {
                rc = harvest_current_serial(challenge, *out);
            }
            if (rc != EHEM_OK) {
                ehem_checkin_result_free(*out);
                *out = NULL;
                rc = ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
            }
        }
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
    free(result->newcrt_chain);
    free(result->current_serial);
    free(result);
}

/* -------------------------------------------------------------------------- */
/* certificate inspection (REQ-SYS-006 / REQ-TOOL-003)                        */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_cert_inspect(ehem_ctx *ctx, const char *crt_b64,
                          ehem_cert_info **out)
{
    uint8_t *der = NULL;
    size_t b64_len;
    size_t der_len;
    ehem_cert_fields f;
    ehem_cert_info *info;
    ehem_rc rc;

    if (ctx == NULL || crt_b64 == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    b64_len = strlen(crt_b64);
    if (b64_len == 0) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             "cert: empty certificate string");
    }
    der = malloc(b64_len);              /* decoded is never larger than input */
    if (der == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    der_len = ehem_b64_std_decode(crt_b64, b64_len, der, b64_len);
    if (der_len == (size_t)-1) {
        free(der);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             "cert: not valid base64");
    }
    rc = ehem_cert_parse_leaf(der, der_len, &f);
    free(der);
    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             "cert: could not parse X.509 leaf certificate");
    }

    info = calloc(1, sizeof *info);
    if (info == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    info->serial     = dup_str(f.serial_hex);
    info->subject_cn = dup_str(f.subject_cn);
    info->not_before = dup_str(f.not_before);
    info->not_after  = dup_str(f.not_after);
    if (info->serial == NULL || info->subject_cn == NULL ||
        info->not_before == NULL || info->not_after == NULL) {
        ehem_cert_info_free(info);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    *out = info;
    return EHEM_OK;
}

void ehem_cert_info_free(ehem_cert_info *info)
{
    if (info == NULL) {
        return;
    }
    free(info->serial);
    free(info->subject_cn);
    free(info->not_before);
    free(info->not_after);
    free(info);
}

/* -------------------------------------------------------------------------- */
/* config (REQ-SYS-004)                                                       */
/* -------------------------------------------------------------------------- */

static ehem_rc parse_config(ehem_ctx *ctx, const ehem_json *root,
                            ehem_config_info **out)
{
    ehem_config_info *c = calloc(1, sizeof *c);
    const char *str;
    const char *missing = NULL;

    if (c == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Required strings. */
    if (!ehem_json_get_string(root, "devid", &str)) {
        missing = "devid";
    } else if ((c->devid = dup_str(str)) == NULL) {
        goto oom;
    } else if (!ehem_json_get_string(root, "hostname", &str)) {
        missing = "hostname";
    } else if ((c->hostname = dup_str(str)) == NULL) {
        goto oom;
    } else if (!ehem_json_get_string(root, "user", &str)) {
        missing = "user";
    } else if ((c->user = dup_str(str)) == NULL) {
        goto oom;
    }
    if (missing != NULL) {
        ehem_system_config_free(c);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             "system/config: missing required field '%s'", missing);
    }

    /* Optional strings (email may be present but empty — kept as ""). */
    if (!opt_str(root, "email", &c->email) ||
        !opt_str(root, "eid", &c->eid) ||
        !opt_str(root, "instanceid", &c->instanceid) ||
        !opt_str(root, "origin", &c->origin) ||
        !opt_str(root, "ip", &c->ip) ||
        !opt_str(root, "genuine_id", &c->genuine_id)) {
        goto oom;
    }

    /* Optional numbers (has_* distinguishes 0 from absent). */
    if (ehem_json_get_int64(root, "iat", &c->iat))               { c->has_iat = true; }
    if (ehem_json_get_int64(root, "uts", &c->uts))               { c->has_uts = true; }
    if (ehem_json_get_int64(root, "ctx", &c->ctx))               { c->has_ctx = true; }
    if (ehem_json_get_int64(root, "storage_mode", &c->storage_mode)) {
        c->has_storage_mode = true;
    }
    if (ehem_json_get_int64(root, "storage_disk0size", &c->storage_disk0size)) {
        c->has_storage_disk0size = true;
    }
    if (ehem_json_get_int64(root, "storage_capacity", &c->storage_capacity)) {
        c->has_storage_capacity = true;
    }

    /* Optional booleans (has_* distinguishes false from absent). */
    if (ehem_json_get_bool(root, "dnsd", &c->dnsd))                       { c->has_dnsd = true; }
    if (ehem_json_get_bool(root, "trusted_ts", &c->trusted_ts))           { c->has_trusted_ts = true; }
    if (ehem_json_get_bool(root, "trusted_backend", &c->trusted_backend)) { c->has_trusted_backend = true; }
    if (ehem_json_get_bool(root, "allow_keysearch", &c->allow_keysearch)) { c->has_allow_keysearch = true; }
    if (ehem_json_get_bool(root, "http_option_hsts", &c->http_hsts))      { c->has_http_hsts = true; }

    *out = c;
    return EHEM_OK;

oom:
    ehem_system_config_free(c);
    return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
}

ehem_rc ehem_system_config(ehem_ctx *ctx, ehem_config_info **out)
{
    ehem_json *root = NULL;
    ehem_rc rc;

    if (ctx == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;
    ehem_ctx_clear_error(ctx);

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, "/api/system/config",
                                 NULL, "system:config", EHEM_TLS_REQ_DEFAULT, &root);
    if (rc != EHEM_OK) {
        return rc;
    }
    rc = parse_config(ctx, root, out);
    ehem_json_free(root);
    return rc;
}

void ehem_system_config_free(ehem_config_info *config)
{
    if (config == NULL) {
        return;
    }
    free(config->devid);
    free(config->hostname);
    free(config->user);
    free(config->email);
    free(config->eid);
    free(config->instanceid);
    free(config->origin);
    free(config->ip);
    free(config->genuine_id);
    free(config);
}

ehem_rc ehem_system_config_install_cert(ehem_ctx *ctx, const char *crt_b64,
                                        ehem_cert_install_info **out)
{
    ehem_json *root = NULL;
    ehem_json *body_obj;
    ehem_json *tls;
    char *body;
    ehem_rc rc;

    if (ctx == NULL || crt_b64 == NULL) {
        return EHEM_ERR_ARG;
    }
    if (out != NULL) {
        *out = NULL;
    }
    ehem_ctx_clear_error(ctx);

    /* Build the cert-only body {"tls":{"crt":"<crt_b64>"}} through the JSON
     * layer (REQ-BUILD-003) — replaces the stored cert, keeps the private key. */
    body_obj = ehem_json_new_object();
    if (body_obj == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    tls = ehem_json_add_object(body_obj, "tls");
    if (tls == NULL || !ehem_json_add_string(tls, "crt", crt_b64)) {
        ehem_json_free(body_obj);
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    body = ehem_json_print(body_obj);
    ehem_json_free(body_obj);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, "/api/system/config",
                                 body, "system:config", EHEM_TLS_REQ_DEFAULT, &root);
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        return rc;   /* 400 validator / 409 in-progress → mapped with detail */
    }

    if (out != NULL) {
        ehem_cert_install_info *info = calloc(1, sizeof *info);
        if (info == NULL) {
            ehem_json_free(root);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
        /* Absent → false (tolerant): reboot_required is only sent when set. */
        ehem_json_get_bool(root, "updated", &info->updated);
        ehem_json_get_bool(root, "reboot_required", &info->reboot_required);
        *out = info;
    }
    ehem_json_free(root);
    return EHEM_OK;
}

void ehem_cert_install_free(ehem_cert_install_info *info)
{
    free(info);
}

/* -------------------------------------------------------------------------- */
/* reboot (REQ-SYS-005)                                                       */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_system_reboot(ehem_ctx *ctx)
{
    char *body = NULL;
    ehem_rc rc;

    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_GET, "/api/system/reboot",
                                NULL, "system:config", EHEM_TLS_REQ_DEFAULT, &body);
    /* The device replies 200 with an EMPTY body and closes the socket, then
     * reboots after a short delay. The shared path reports an empty 2xx body as
     * EHEM_ERR_PROTOCOL (http_status 200) — for a reboot that IS success. */
    if (rc == EHEM_OK) {
        free(body);                 /* a body is not expected, but tolerate one */
    } else if (rc == EHEM_ERR_PROTOCOL && ehem_last_error(ctx)->http_status == 200) {
        rc = EHEM_OK;
    } else {
        return rc;                  /* real failure: 401/403/transport/etc. */
    }

    /* A reboot invalidates every token the device issued — drop the whole cache
     * so the next authenticated call re-logs-in (REQ-SYS-005, REQ-AUTH-002). */
    ehem_auth_invalidate(ctx, NULL);
    ehem_ctx_clear_error(ctx);      /* leave a clean success state */
    return EHEM_OK;
}
