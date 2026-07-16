/*
 * proto_common.h — shared request/response machinery for protocol bindings.
 *
 * implements: REQ-AUTH-003 (the authenticated request path — bearer injection
 *             for a scoped request, single 401 re-acquire+retry, 403 →
 *             SCOPE_DENIED), REQ-NET-005 (the automatic certificate recovery
 *             lives in the common send path so every binding — M1 system, M2+
 *             auth/keymgmt — inherits it), and the REQ-API-003 HTTP→rc mapping
 *             shared by all bindings.
 *
 * INTERNAL header. A binding calls ehem_proto_request_json() (or _raw for
 * verbatim-body flows like check-in) and gets back either a parsed JSON
 * object / raw body, or a mapped ehem_rc with last-error detail already
 * recorded on the context.
 */
#ifndef EHEM_PROTO_COMMON_H
#define EHEM_PROTO_COMMON_H

#include "context.h"
#include "json.h"
#include "transport.h"

/* Public typedef lives in <ehem/system.h>; forward-declared here so this
 * header need not pull a public binding header. */
struct ehem_checkin_info;

/* Map a non-2xx HTTP status to an ehem_rc (REQ-API-003). Refined as auth lands
 * in later milestones; for M1 it covers the unauthenticated system endpoints. */
ehem_rc ehem_proto_map_http_status(long status);

/* A key id on the wire is exactly 32 hex chars (16 bytes). Shared by every
 * binding that takes a kid (keymgmt, crypto). Stops at the first non-hex
 * byte, so a short string is safe. */
#define EHEM_PROTO_KID_HEX_LEN 32
bool ehem_proto_is_kid_hex(const char *s);

/*
 * Perform one JSON exchange: send `method` to `path` (relative to the device
 * base URL, or an absolute http(s):// URL for the check-in cloud leg) with an
 * optional NUL-terminated JSON body, and return the response body in
 * *body_out (malloc'd, NUL-terminated; caller frees).
 *
 * `scope` selects the authentication posture (REQ-AUTH-003):
 *   - NULL       → unauthenticated request (status, version, check-in, and the
 *                  login exchange itself); no Authorization header is sent.
 *   - non-NULL   → obtain a bearer token for that scope from the cache
 *                  (REQ-AUTH-002) and send `Authorization: Bearer <token>`. A
 *                  401 with that token triggers exactly one re-acquire + retry
 *                  (the token was invalidated server-side); a second 401 maps
 *                  to EHEM_ERR_AUTH_FAILED.
 *
 * Handles uniformly, recording ehem_last_error detail and returning the
 * mapped rc on every failure path:
 *   - token acquisition failure for a non-NULL scope (EHEM_ERR_AUTH_* etc.);
 *   - transport-level failure (incl. the automatic expired-cert check-in
 *     recovery + single retry, REQ-NET-005 — unless this request itself is
 *     part of a check-in, ctx->in_checkin);
 *   - non-2xx HTTP status (ehem_proto_map_http_status + device payload);
 *   - empty response body (EHEM_ERR_PROTOCOL).
 * The auth retry and the check-in retry compose but do not multiply: at most
 * one of each per request.
 *
 * `tls_override` is EHEM_TLS_REQ_DEFAULT for normal binding traffic; the
 * check-in flow passes RELAX (device legs) / VERIFY (cloud leg).
 */
ehem_rc ehem_proto_request_raw(ehem_ctx *ctx, ehem_http_method method,
                               const char *path, const char *json_body,
                               const char *scope,
                               ehem_tls_req_override tls_override,
                               char **body_out);

/*
 * ehem_proto_request_raw + parse: hands back the response as a parsed JSON
 * object in *root_out (caller frees with ehem_json_free). A malformed or
 * non-object body is EHEM_ERR_PROTOCOL with detail. `scope` has the same
 * meaning as in ehem_proto_request_raw.
 */
ehem_rc ehem_proto_request_json(ehem_ctx *ctx, ehem_http_method method,
                                const char *path, const char *json_body,
                                const char *scope,
                                ehem_tls_req_override tls_override,
                                ehem_json **root_out);

/*
 * The three-leg check-in flow (REQ-SYS-003), shared by the public
 * ehem_system_checkin() and the automatic recovery hook. Implemented in
 * proto_system.c (it is a `system` group endpoint). `relax_device_tls`
 * selects RELAX for the device legs — true when recovering from an invalid
 * certificate, false for an explicit call on a healthy connection.
 * `out` may be NULL when the caller only needs the side effect.
 */
ehem_rc ehem_checkin_run(ehem_ctx *ctx, int relax_device_tls,
                         struct ehem_checkin_info **out);

#endif /* EHEM_PROTO_COMMON_H */
