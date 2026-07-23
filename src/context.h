/*
 * context.h — internal definition of ehem_ctx and error-detail helpers.
 *
 * implements: REQ-API-001, REQ-API-002, REQ-API-004
 *
 * INTERNAL header. The public header (include/ehem/ehem.h) forward-declares
 * `struct ehem_ctx`; its full layout lives here so protocol bindings and the
 * transport can reach context state (URL, timeouts, TLS options, last-error
 * storage) without that layout becoming ABI. All per-connection mutable state
 * is reachable only from this struct (REQ-API-002).
 */
#ifndef EHEM_CONTEXT_H
#define EHEM_CONTEXT_H

#include <stdbool.h>

#include "ehem/ehem.h"

/* Capacity of the inline last-error message buffer (backs ehem_error.message). */
#define EHEM_ERR_MSG_MAX 256

/* Session/auth state (passphrase + scope-keyed token cache). Opaque here — the
 * full layout lives in proto_auth.c so credential handling stays contained to
 * the auth component (REQ-AUTH-002). NULL until the first ehem_login(). */
struct ehem_auth;

struct ehem_ctx {
    /* Immutable connection configuration (set at create, owned copies). */
    char          *url;
    long           connect_timeout_ms;
    long           total_timeout_ms;
    long           request_pace_ms;      /* min delay before each request (REQ-NET-006); 0 = none */
    ehem_tls_mode  tls_mode;
    char          *ca_file;              /* owned copy, or NULL when unused */

    /* Effective transport all requests go through (REQ-NET-001). It is either a
     * caller-supplied override (borrowed — owns_transport false) or the built-in
     * default this context allocates (owns_transport true, torn down on
     * destroy). NULL until a transport is set (the default is wired in
     * STEP-M1-060). Reach it via ehem_ctx_transport(). */
    const ehem_transport *transport;
    bool                  owns_transport;

    /* Check-in / automatic certificate recovery (REQ-SYS-003, REQ-NET-005). */
    char *checkin_url;     /* owned; cloud endpoint (default EHEM_DEFAULT_CHECKIN_URL) */
    bool  no_auto_checkin; /* opt-out of automatic expired-cert recovery */
    bool  cert_refreshed;  /* sticky: an automatic recovery refreshed the cert */
    bool  in_checkin;      /* recursion guard: check-in legs never auto-recover */

    /* Proactive session-start check-in (REQ-AUTH-005). `done` latches after the
     * one attempt so the check-in runs at most once per context. */
    bool  checkin_on_login;
    bool  checkin_on_login_done;

    /* Mobile confirmation timeout (REQ-AUTH-010), per token acquisition. */
    long  confirm_timeout_ms;

    /* Auth / session (REQ-AUTH-001, REQ-AUTH-002). `auth` is lazily allocated
     * by ehem_login() and torn down (zeroizing credentials) by ehem_logout() /
     * ehem_ctx_destroy(). no_credential_retention mirrors the option. */
    struct ehem_auth *auth;
    bool  no_credential_retention;

    /* Last-error detail (REQ-API-004). `last_error` is what ehem_last_error()
     * returns; its string pointers reference the two buffers below, both owned
     * by the context. Valid until the next API call mutates them. */
    ehem_error  last_error;
    char       *err_payload;                  /* heap copy backing device_payload, or NULL */
    char        err_message[EHEM_ERR_MSG_MAX]; /* backing store for last_error.message */
};

/*
 * The transport this context sends through — a caller override if one was set
 * in options, otherwise the built-in default (wired in STEP-M1-060). May be
 * NULL if no transport is available yet. Bindings send via this.
 */
const ehem_transport *ehem_ctx_transport(const ehem_ctx *ctx);

/*
 * Reset the context's last-error detail to the "success" state: http_status 0,
 * device_payload NULL (freeing any prior copy), message "". Bindings call this
 * at the start of a call and leave it as-is on success.
 */
void ehem_ctx_clear_error(ehem_ctx *ctx);

/*
 * Record a failure on the context and return `rc` (so a binding can write
 * `return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL, "missing %s", key);`).
 *   - http_status:    HTTP status to expose, or 0 if not applicable.
 *   - device_payload: device error body to copy (a NUL-terminated string), or
 *                     NULL. Copied verbatim into the last-error detail.
 *   - fmt, ...:       printf-style human-readable message (never stored NULL).
 * NULL-safe on ctx (returns rc unchanged).
 */
ehem_rc ehem_ctx_fail(ehem_ctx *ctx, ehem_rc rc, long http_status,
                      const char *device_payload,
                      const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

#endif /* EHEM_CONTEXT_H */
