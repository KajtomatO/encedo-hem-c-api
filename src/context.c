/*
 * context.c — ehem_ctx lifecycle, options, error enum, last-error, global init.
 *
 * implements: REQ-API-001, REQ-API-002, REQ-API-003, REQ-API-004
 */
#include "context.h"
#include "crypto_shim.h"
#include "proto_auth.h"
#include "transport.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Error enum → string (REQ-API-003)                                          */
/* -------------------------------------------------------------------------- */

const char *ehem_rc_str(ehem_rc rc)
{
    switch (rc) {
    case EHEM_OK:                return "EHEM_OK";
    case EHEM_ERR_NETWORK:       return "EHEM_ERR_NETWORK";
    case EHEM_ERR_UNREACHABLE:   return "EHEM_ERR_UNREACHABLE";
    case EHEM_ERR_AUTH_EXPIRED:  return "EHEM_ERR_AUTH_EXPIRED";
    case EHEM_ERR_AUTH_FAILED:   return "EHEM_ERR_AUTH_FAILED";
    case EHEM_ERR_SCOPE_DENIED:  return "EHEM_ERR_SCOPE_DENIED";
    case EHEM_ERR_USER_REJECTED: return "EHEM_ERR_USER_REJECTED";
    case EHEM_ERR_CONFIRM_TIMEOUT: return "EHEM_ERR_CONFIRM_TIMEOUT";
    case EHEM_ERR_NOT_FOUND:     return "EHEM_ERR_NOT_FOUND";
    case EHEM_ERR_DEVICE:        return "EHEM_ERR_DEVICE";
    case EHEM_ERR_PROTOCOL:      return "EHEM_ERR_PROTOCOL";
    case EHEM_ERR_ARG:           return "EHEM_ERR_ARG";
    case EHEM_ERR_NOMEM:         return "EHEM_ERR_NOMEM";
    case EHEM_ERR_UNSUPPORTED:   return "EHEM_ERR_UNSUPPORTED";
    }
    /* Out-of-range value (never for a valid ehem_rc). */
    return "EHEM_ERR_UNKNOWN";
}

/* -------------------------------------------------------------------------- */
/* Small internal helpers                                                     */
/* -------------------------------------------------------------------------- */

/* strdup is POSIX, not C99 — provide our own so strict -std=c99 stays clean. */
static char *ehem_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* A URL is accepted only if it names an http:// or https:// endpoint with a
 * non-empty host part. Deeper validation is the transport's job. */
static bool url_is_valid(const char *url)
{
    const char *rest;
    if (url == NULL) {
        return false;
    }
    if (strncmp(url, "https://", 8) == 0) {
        rest = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        rest = url + 7;
    } else {
        return false;
    }
    return *rest != '\0';
}

/* True iff the caller's abi_size covers field `f` in full — the size/version
 * guard that lets ehem_options grow append-only without breaking the ABI. */
#define EHEM_OPT_HAS(opts, f) \
    ((opts)->abi_size >= offsetof(ehem_options, f) + sizeof((opts)->f))

/* -------------------------------------------------------------------------- */
/* Options (REQ-API-001, REQ-NET-003, REQ-NET-004)                            */
/* -------------------------------------------------------------------------- */

void ehem_options_init(ehem_options *opts)
{
    if (opts == NULL) {
        return;
    }
    opts->abi_size           = sizeof(*opts);
    opts->connect_timeout_ms = EHEM_DEFAULT_CONNECT_TIMEOUT_MS;
    opts->total_timeout_ms   = EHEM_DEFAULT_TOTAL_TIMEOUT_MS;
    opts->tls_mode           = EHEM_TLS_SYSTEM;
    opts->ca_file            = NULL;
    opts->transport          = NULL;
    opts->no_auto_checkin    = 0;      /* automatic cert recovery on by default */
    opts->checkin_url        = NULL;   /* NULL → EHEM_DEFAULT_CHECKIN_URL */
    opts->no_credential_retention = 0; /* retain the passphrase for silent refresh */
}

/* Copy options into the context, applying defaults for absent/zero fields.
 * Returns EHEM_ERR_NOMEM on allocation failure, EHEM_ERR_ARG on an invalid
 * combination (CA_FILE mode without a ca_file). */
static ehem_rc apply_options(ehem_ctx *ctx, const ehem_options *opts)
{
    ctx->connect_timeout_ms = EHEM_DEFAULT_CONNECT_TIMEOUT_MS;
    ctx->total_timeout_ms   = EHEM_DEFAULT_TOTAL_TIMEOUT_MS;
    ctx->tls_mode           = EHEM_TLS_SYSTEM;
    ctx->ca_file            = NULL;
    ctx->transport          = NULL;
    ctx->owns_transport     = false;
    ctx->no_auto_checkin    = false;
    ctx->checkin_url        = NULL;   /* set below (owned copy) */
    ctx->no_credential_retention = false;

    if (opts == NULL) {
        ctx->checkin_url = ehem_strdup(EHEM_DEFAULT_CHECKIN_URL);
        return (ctx->checkin_url != NULL) ? EHEM_OK : EHEM_ERR_NOMEM;
    }

    if (EHEM_OPT_HAS(opts, connect_timeout_ms) && opts->connect_timeout_ms > 0) {
        ctx->connect_timeout_ms = opts->connect_timeout_ms;
    }
    if (EHEM_OPT_HAS(opts, total_timeout_ms) && opts->total_timeout_ms > 0) {
        ctx->total_timeout_ms = opts->total_timeout_ms;
    }
    if (EHEM_OPT_HAS(opts, tls_mode)) {
        ctx->tls_mode = opts->tls_mode;
    }
    if (EHEM_OPT_HAS(opts, ca_file) && opts->ca_file != NULL) {
        ctx->ca_file = ehem_strdup(opts->ca_file);
        if (ctx->ca_file == NULL) {
            return EHEM_ERR_NOMEM;
        }
    }
    if (EHEM_OPT_HAS(opts, transport) && opts->transport != NULL) {
        /* Caller override: borrowed, so the context must not destroy it. */
        ctx->transport      = opts->transport;
        ctx->owns_transport = false;
    }
    if (EHEM_OPT_HAS(opts, no_auto_checkin)) {
        ctx->no_auto_checkin = (opts->no_auto_checkin != 0);
    }
    if (EHEM_OPT_HAS(opts, no_credential_retention)) {
        ctx->no_credential_retention = (opts->no_credential_retention != 0);
    }
    ctx->checkin_url = ehem_strdup(
        (EHEM_OPT_HAS(opts, checkin_url) && opts->checkin_url != NULL)
            ? opts->checkin_url
            : EHEM_DEFAULT_CHECKIN_URL);
    if (ctx->checkin_url == NULL) {
        return EHEM_ERR_NOMEM;
    }

    /* CA_FILE trust mode is meaningless without a certificate to trust. */
    if (ctx->tls_mode == EHEM_TLS_CA_FILE && ctx->ca_file == NULL) {
        return EHEM_ERR_ARG;
    }
    return EHEM_OK;
}

/* -------------------------------------------------------------------------- */
/* Last-error detail (REQ-API-004)                                            */
/* -------------------------------------------------------------------------- */

void ehem_ctx_clear_error(ehem_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    free(ctx->err_payload);
    ctx->err_payload               = NULL;
    ctx->last_error.http_status    = 0;
    ctx->last_error.device_payload = NULL;
    ctx->err_message[0]            = '\0';
    ctx->last_error.message        = ctx->err_message;
}

ehem_rc ehem_ctx_fail(ehem_ctx *ctx, ehem_rc rc, long http_status,
                      const char *device_payload,
                      const char *fmt, ...)
{
    va_list ap;

    if (ctx == NULL) {
        return rc;
    }

    va_start(ap, fmt);
    if (fmt != NULL) {
        vsnprintf(ctx->err_message, sizeof ctx->err_message, fmt, ap);
    } else {
        ctx->err_message[0] = '\0';
    }
    va_end(ap);
    ctx->last_error.message     = ctx->err_message;
    ctx->last_error.http_status = http_status;

    /* Replace any prior device payload with a fresh copy. */
    free(ctx->err_payload);
    ctx->err_payload               = NULL;
    ctx->last_error.device_payload = NULL;
    if (device_payload != NULL) {
        size_t len = strlen(device_payload);
        char *copy = malloc(len + 1);
        if (copy != NULL) {
            memcpy(copy, device_payload, len + 1);
            ctx->err_payload               = copy;
            ctx->last_error.device_payload = copy;
        }
        /* On copy failure the rc + message still stand; payload stays NULL. */
    }
    return rc;
}

const ehem_error *ehem_last_error(const ehem_ctx *ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return &ctx->last_error;
}

const ehem_transport *ehem_ctx_transport(const ehem_ctx *ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->transport;
}

int ehem_cert_refreshed(const ehem_ctx *ctx)
{
    return (ctx != NULL && ctx->cert_refreshed) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Context lifecycle (REQ-API-001, REQ-API-002)                               */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_ctx_create(const char *url, const ehem_options *opts, ehem_ctx **out)
{
    ehem_ctx *ctx;
    ehem_rc   rc;

    if (out == NULL) {
        return EHEM_ERR_ARG;
    }
    *out = NULL;

    if (!url_is_valid(url)) {
        return EHEM_ERR_ARG;
    }
    /* A non-NULL options block must have been stamped by ehem_options_init(). */
    if (opts != NULL && opts->abi_size < sizeof(opts->abi_size)) {
        return EHEM_ERR_ARG;
    }

    ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) {
        return EHEM_ERR_NOMEM;
    }
    /* Start with an empty (success) last-error view. */
    ctx->last_error.message = ctx->err_message;

    rc = apply_options(ctx, opts);
    if (rc != EHEM_OK) {
        ehem_ctx_destroy(ctx);
        return rc;
    }

    ctx->url = ehem_strdup(url);
    if (ctx->url == NULL) {
        ehem_ctx_destroy(ctx);
        return EHEM_ERR_NOMEM;
    }

    /* No caller override → stand up the built-in default transport (libcurl),
     * owned by and torn down with this context. */
    if (ctx->transport == NULL) {
        ctx->transport = ehem_transport_default_new(ctx->url, ctx->tls_mode,
                                                    ctx->ca_file);
        if (ctx->transport == NULL) {
            ehem_ctx_destroy(ctx);
            return EHEM_ERR_NOMEM;
        }
        ctx->owns_transport = true;
    }

    *out = ctx;
    return EHEM_OK;
}

void ehem_ctx_destroy(ehem_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* Tear down the transport only if this context created it (the built-in
     * default). A caller-supplied override is borrowed and stays the caller's. */
    if (ctx->owns_transport && ctx->transport != NULL) {
        ehem_transport_destroy((ehem_transport *)ctx->transport);
    }
    /* Destroy implies logout: scrub + free the retained passphrase and drop the
     * token cache (REQ-AUTH-002, REQ-API-001 zeroization). */
    ehem_auth_destroy(ctx->auth);
    free(ctx->url);
    free(ctx->ca_file);
    free(ctx->checkin_url);
    free(ctx->err_payload);
    /* Zeroize before releasing: credential material lives behind ctx->auth
     * (already scrubbed above); wiping the struct clears dangling pointers. */
    memset(ctx, 0, sizeof *ctx);
    free(ctx);
}

/* -------------------------------------------------------------------------- */
/* Process-global init / cleanup (REQ-API-002)                                */
/* -------------------------------------------------------------------------- */

/* The ONLY mutable file-scope state in the library, and the sanctioned
 * exception in REQ-API-002: it guards the process-global backend init so the
 * wrappers below are idempotent. The raw global steps live in the backends
 * (curl_global_init/cleanup, wolfCrypt_Init/Cleanup) so this file stays free of
 * libcurl and wolfSSL. Not thread-safe — call at startup and shutdown
 * (documented in the public header). */
static bool g_global_ready = false;

ehem_rc ehem_global_init(void)
{
    if (!g_global_ready) {
        ehem_rc rc = ehem_crypto_backend_global_init();
        if (rc != EHEM_OK) {
            return rc;
        }
        rc = ehem_transport_backend_global_init();
        if (rc != EHEM_OK) {
            ehem_crypto_backend_global_cleanup();
            return rc;
        }
        g_global_ready = true;
    }
    return EHEM_OK;
}

void ehem_global_cleanup(void)
{
    if (g_global_ready) {
        ehem_transport_backend_global_cleanup();
        ehem_crypto_backend_global_cleanup();
        g_global_ready = false;
    }
}
