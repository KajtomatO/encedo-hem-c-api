/*
 * fake_transport.h — an in-memory ehem_transport for offline unit tests.
 *
 * supports: REQ-TEST-001 (the unit suite drives every binding through this,
 *           never the network) and REQ-NET-001 (proves the vtable seam).
 *
 * Two capabilities:
 *   - script responses: queue a sequence of canned outcomes, each either a
 *     transport-level error (EHEM_ERR_UNREACHABLE / _NETWORK) or an HTTP
 *     status + body;
 *   - capture requests: every outgoing request is recorded (method, path,
 *     headers, body) so a test can assert on what a binding sent.
 *
 * Create with fake_transport_new(), put the returned handle in
 * ehem_options.transport, and free it with fake_transport_free() AFTER the
 * context is destroyed (the context borrows it, it does not own it).
 */
#ifndef EHEM_FAKE_TRANSPORT_H
#define EHEM_FAKE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"
#include "transport.h"   /* internal: ehem_transport, ehem_http_method, ... */

/* A recorded outgoing request (deep copies of everything the binding sent). */
typedef struct fake_captured_request {
    ehem_http_method method;
    char            *path;
    ehem_header     *headers;      /* owned copies */
    size_t           header_count;
    uint8_t         *body;         /* owned copy, NUL-terminated; NULL if none */
    size_t           body_len;
    ehem_tls_req_override tls_override;  /* per-request TLS posture the caller asked for */
    int              fresh_connection;
} fake_captured_request;

/* Construct a fake transport. Returns NULL on allocation failure. */
ehem_transport *fake_transport_new(void);

/* Destroy a fake transport and everything it captured/queued. NULL-safe. */
void fake_transport_free(ehem_transport *t);

/*
 * Queue the next canned outcome (FIFO). If rc == EHEM_OK, send() returns EHEM_OK
 * with the given HTTP status and (copied) body. If rc is any error, send()
 * returns that error and leaves the response zeroed (a transport failure).
 * `body` may be NULL. Returns 0 on success, -1 on allocation failure.
 */
int fake_transport_push_response(ehem_transport *t, ehem_rc rc,
                                 long status, const char *body);

/*
 * Queue a transport failure that reports "peer certificate expired": send()
 * returns `rc` (typically EHEM_ERR_NETWORK) and ehem_transport_last_tls_expired
 * reports 1 until the next send — the REQ-NET-005 auto-recovery trigger.
 * Returns 0 on success, -1 on allocation failure.
 */
int fake_transport_push_tls_expired(ehem_transport *t, ehem_rc rc);

/* If send() is called with no queued response, it returns this rc (default
 * EHEM_ERR_NETWORK) — set it to make "ran out of script" explicit in a test. */
void fake_transport_set_default_rc(ehem_transport *t, ehem_rc rc);

/* Set the string ehem_transport_last_detail() will return after a failing send
 * (default ""), so a test can assert a binding surfaces transport detail. */
void fake_transport_set_detail(ehem_transport *t, const char *detail);

/* Number of requests captured so far. */
size_t fake_transport_request_count(const ehem_transport *t);

/* The i-th captured request (0-based), or NULL if out of range. */
const fake_captured_request *fake_transport_request(const ehem_transport *t,
                                                    size_t i);

/* Convenience: value of the named header on the i-th captured request, or NULL
 * if absent (case-sensitive match). */
const char *fake_transport_request_header(const ehem_transport *t, size_t i,
                                          const char *name);

#endif /* EHEM_FAKE_TRANSPORT_H */
