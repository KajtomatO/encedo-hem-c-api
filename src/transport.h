/*
 * transport.h — internal transport vtable: the single seam all HTTP(S) traffic
 * passes through.
 *
 * implements: REQ-NET-001
 *
 * INTERNAL header. The public header forward-declares `struct ehem_transport`
 * so a context option can carry an override pointer; the full vtable lives here
 * and is realized by exactly two implementations — the libcurl default
 * (transport_curl.c, STEP-M1-060) and the unit-test fake (tests/support). The
 * transport knows nothing about JSON or authentication: it maps a prepared
 * request to a response and translates connection-level failure into
 * EHEM_ERR_UNREACHABLE / EHEM_ERR_NETWORK. HTTP error *statuses* (4xx/5xx) are
 * NOT transport errors — the body is returned and the binding above maps them.
 */
#ifndef EHEM_TRANSPORT_H
#define EHEM_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"   /* ehem_rc, forward-declared ehem_transport */

/* HTTP methods used by the bindings. */
typedef enum ehem_http_method {
    EHEM_HTTP_GET = 0,
    EHEM_HTTP_POST,
    EHEM_HTTP_PUT,
    EHEM_HTTP_DELETE,
    EHEM_HTTP_PATCH
} ehem_http_method;

/* One header line. In a request the strings are borrowed from the caller; in a
 * response they are owned by the response and released by ehem_response_free. */
typedef struct ehem_header {
    const char *name;
    const char *value;
} ehem_header;

/* An outgoing request. `path` is appended to the context's base URL by the
 * transport. Timeouts travel per-request (REQ-NET-001) so a later long-wait
 * flow (mobile confirm, M8) needs no vtable change. */
typedef struct ehem_request {
    ehem_http_method   method;
    const char        *path;               /* e.g. "/api/system/status" */
    const ehem_header *headers;            /* array, or NULL */
    size_t             header_count;
    const uint8_t     *body;               /* request body, or NULL */
    size_t             body_len;
    long               connect_timeout_ms; /* <=0 → transport default */
    long               total_timeout_ms;   /* <=0 → transport default */
} ehem_request;

/* A response. `body` is NUL-terminated for convenience (body_len excludes the
 * terminator) but may contain arbitrary bytes up to body_len. All pointers are
 * owned by the response; release with ehem_response_free. */
typedef struct ehem_response {
    long          status;        /* HTTP status code, or 0 if none was received */
    ehem_header  *headers;       /* owned array, or NULL */
    size_t        header_count;
    uint8_t      *body;          /* owned, NUL-terminated, or NULL */
    size_t        body_len;
} ehem_response;

/*
 * Transport operations bound to per-instance state.
 *
 * send(): perform one request→response.
 *   - On a connection-level failure return EHEM_ERR_UNREACHABLE (could not
 *     connect: refused / DNS / connect timeout) or EHEM_ERR_NETWORK
 *     (established then failed / total timeout), and leave *resp zeroed.
 *   - Otherwise return EHEM_OK with *resp populated — even for a 4xx/5xx HTTP
 *     status, which is the binding's to interpret. The caller owns *resp and
 *     frees it with ehem_response_free.
 * destroy(): release per-instance state (not the ehem_transport wrapper — that
 *   is freed by ehem_transport_destroy).
 */
typedef struct ehem_transport_ops {
    ehem_rc (*send)(void *state, const ehem_request *req, ehem_response *resp);
    /* Optional: a human-readable detail string for the most recent send()
     * failure (e.g. libcurl's error text). May be NULL, and may return NULL or
     * "" when there is nothing to add. Valid until the next send() on the same
     * instance. Bindings surface it through ehem_last_error (REQ-API-004). */
    const char *(*last_detail)(void *state);
    void    (*destroy)(void *state);
} ehem_transport_ops;

/* A transport instance: an ops table plus its private state. Heap-allocated by
 * its constructor (the curl default or the fake) and torn down by
 * ehem_transport_destroy. */
struct ehem_transport {
    const ehem_transport_ops *ops;
    void                     *state;
};

/* Dispatch one request through `t`. NULL transport → EHEM_ERR_ARG. */
ehem_rc ehem_transport_send(const ehem_transport *t,
                            const ehem_request *req,
                            ehem_response *resp);

/* Human-readable detail of the last send() failure on `t`, or "" if none /
 * unsupported. Never NULL. Valid until the next send() on the same instance. */
const char *ehem_transport_last_detail(const ehem_transport *t);

/* -------------------------------------------------------------------------- */
/* Default transport backend (libcurl). Declared here with a libcurl-free      */
/* signature so context.c can create/init it without seeing any curl type;     */
/* the implementation lives in transport_curl.c (STEP-M1-060).                 */
/* implements: REQ-NET-002                                                     */
/* -------------------------------------------------------------------------- */

/* Construct the built-in default transport for a context with the given base
 * URL and TLS settings (REQ-NET-002, REQ-NET-003). Returns NULL on failure. */
ehem_transport *ehem_transport_default_new(const char *base_url,
                                           ehem_tls_mode tls_mode,
                                           const char *ca_file);

/* Process-global init/cleanup for the default backend (wraps curl_global_*).
 * Idempotency is the caller's (ehem_global_init/cleanup); these run the raw
 * global step exactly when told. init returns EHEM_OK or an error rc. */
ehem_rc ehem_transport_backend_global_init(void);
void    ehem_transport_backend_global_cleanup(void);

/* Tear down a transport instance created by a constructor. NULL-safe. Calls
 * ops->destroy(state) then frees the wrapper. Never call this on a
 * caller-supplied override — the caller owns that one. */
void ehem_transport_destroy(ehem_transport *t);

/* Release the contents of a response filled by send(). NULL-safe; leaves the
 * struct zeroed so double-free is safe. */
void ehem_response_free(ehem_response *resp);

#endif /* EHEM_TRANSPORT_H */
