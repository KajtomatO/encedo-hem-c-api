/*
 * ehem.h — Encedo HEM C SDK, top-level public header.
 *
 * Part of encedo-hem-c-api. MIT licensed, written from scratch
 * (ARCHITECTURE.md §1). Public symbols all carry the `ehem_` prefix.
 */
#ifndef EHEM_H
#define EHEM_H

#include <stddef.h>   /* size_t */

/*
 * EHEM_API — symbol-visibility / export-control macro.
 * implements: REQ-API-006
 *
 * Default visibility is hidden (the library is compiled with
 * -fvisibility=hidden / C_VISIBILITY_PRESET hidden); only symbols tagged
 * EHEM_API are exported from the shared library. On Windows the same macro
 * carries __declspec(dllexport/dllimport).
 *
 *   - Building the shared library : the build defines EHEM_BUILDING_SHARED.
 *   - Consuming the shared library on Windows: define EHEM_USING_SHARED.
 *   - Static library / internal TUs: EHEM_API expands to nothing.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(EHEM_BUILDING_SHARED)
#    define EHEM_API __declspec(dllexport)
#  elif defined(EHEM_USING_SHARED)
#    define EHEM_API __declspec(dllimport)
#  else
#    define EHEM_API
#  endif
#else
#  if defined(EHEM_BUILDING_SHARED) && (defined(__GNUC__) || defined(__clang__))
#    define EHEM_API __attribute__((visibility("default")))
#  else
#    define EHEM_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime library version, e.g. "0.1.0" (semantic versioning; 0.x until
 * full-spec conformance — ARCHITECTURE.md §4). The returned pointer is a
 * static string owned by the library; the caller must not free it.
 */
EHEM_API const char *ehem_version(void);

/* ==========================================================================
 * Error / return codes
 * implements: REQ-API-003
 *
 * Every fallible public function returns an ehem_rc. EHEM_OK is 0; a caller
 * may test `if (rc != EHEM_OK)`. The full set ships now even though several
 * values only become producible in later milestones (auth, scope, confirm) —
 * the ABI surface is fixed early so the PKCS#11 backend can build its error
 * table against a stable enum. Do not renumber or reorder: append only.
 * ========================================================================== */
typedef enum ehem_rc {
    EHEM_OK = 0,               /* success */
    EHEM_ERR_NETWORK,          /* connection established but the exchange failed */
    EHEM_ERR_UNREACHABLE,      /* could not connect (refused, DNS, connect timeout) */
    EHEM_ERR_AUTH_EXPIRED,     /* bearer token / credential expired */
    EHEM_ERR_AUTH_FAILED,      /* authentication rejected (bad passphrase, ...) */
    EHEM_ERR_SCOPE_DENIED,     /* authenticated but not authorized for the scope */
    EHEM_ERR_USER_REJECTED,    /* mobile-app confirmation rejected by the user */
    EHEM_ERR_CONFIRM_TIMEOUT,  /* mobile-app confirmation not answered in time */
    EHEM_ERR_NOT_FOUND,        /* addressed object (e.g. key) does not exist */
    EHEM_ERR_DEVICE,           /* device reported an error not covered above */
    EHEM_ERR_PROTOCOL,         /* malformed/unparseable response or missing field */
    EHEM_ERR_ARG,              /* invalid argument from the caller */
    EHEM_ERR_NOMEM,            /* out of memory */
    EHEM_ERR_UNSUPPORTED       /* operation/parameter not supported by this build */
} ehem_rc;

/*
 * Map any ehem_rc to a stable, static, human-readable identifier string
 * (e.g. "EHEM_ERR_NETWORK"). The returned pointer is owned by the library and
 * must not be freed; it is never NULL, even for an out-of-range value.
 */
EHEM_API const char *ehem_rc_str(ehem_rc rc);

/* ==========================================================================
 * Transport override (forward declaration)
 *
 * The transport vtable type is defined by the transport header (added with the
 * vtable itself). Options below reserve a typed slot for a caller-supplied
 * transport now so the ABI is fixed; until a caller has a concrete transport,
 * leave ehem_options.transport NULL to use the built-in default.
 * implements: REQ-NET-001 (options carry the override slot)
 * ========================================================================== */
typedef struct ehem_transport ehem_transport;

/* ==========================================================================
 * Context options
 * implements: REQ-API-001, REQ-NET-003, REQ-NET-004
 *
 * Size/version discipline (ARCHITECTURE.md §4): `abi_size` is the first field
 * and MUST be stamped by ehem_options_init(). The library reads only the
 * fields the caller's abi_size covers, so the struct can grow (append-only)
 * without breaking either direction of the ABI. Zero is always a valid
 * "use the default" for every scalar field.
 * ========================================================================== */

/* TLS trust modes (REQ-NET-003) — exactly three. */
typedef enum ehem_tls_mode {
    EHEM_TLS_SYSTEM = 0,   /* default: verify against the system trust store */
    EHEM_TLS_CA_FILE,      /* verify against a caller-supplied CA/pinned cert */
    EHEM_TLS_INSECURE      /* explicit opt-in: skip verification (lab use only) */
} ehem_tls_mode;

/* Documented defaults applied by ehem_options_init() / a NULL options arg. */
#define EHEM_DEFAULT_CONNECT_TIMEOUT_MS 10000L   /* 10 s to establish a connection */
#define EHEM_DEFAULT_TOTAL_TIMEOUT_MS   30000L   /* 30 s for the whole request */

/* Default cloud endpoint for the check-in handshake (see ehem_system_checkin
 * in <ehem/system.h> and the automatic recovery notes below). */
#define EHEM_DEFAULT_CHECKIN_URL "https://api.encedo.com/checkin"

typedef struct ehem_options {
    size_t         abi_size;            /* set by ehem_options_init(); do not touch */
    long           connect_timeout_ms;  /* connect timeout; <=0 → default */
    long           total_timeout_ms;    /* whole-request timeout; <=0 → default */
    ehem_tls_mode  tls_mode;            /* default EHEM_TLS_SYSTEM */
    const char    *ca_file;             /* CA bundle / pinned cert path for CA_FILE mode */
    const ehem_transport *transport;    /* transport override; NULL → built-in default */

    /*
     * Automatic certificate recovery. When a device request fails TLS
     * verification because the device certificate has EXPIRED (the one
     * classifiable, check-in-recoverable failure), the SDK runs the check-in
     * handshake to refresh the certificate and retries the request once on a
     * fresh connection. Enabled by default; set no_auto_checkin to a nonzero
     * value to opt out (zero keeps the default, per the abi_size discipline).
     * A performed refresh is reported by ehem_cert_refreshed().
     */
    int            no_auto_checkin;     /* 0 = automatic recovery on (default) */
    const char    *checkin_url;         /* cloud check-in endpoint override;
                                         * NULL → EHEM_DEFAULT_CHECKIN_URL */

    /*
     * Credential retention (REQ-AUTH-002). By default ehem_login() keeps a
     * zeroized copy of the passphrase inside the context so the session engine
     * can silently re-acquire bearer tokens for the context's lifetime (silent
     * refresh). Set no_credential_retention to a nonzero value to keep only the
     * tokens already acquired: the passphrase is scrubbed as soon as it has
     * been used, and a later cache miss/expiry then fails with
     * EHEM_ERR_AUTH_EXPIRED until the caller calls ehem_login() again (zero
     * keeps the default retaining behavior, per the abi_size discipline).
     */
    int            no_credential_retention; /* 0 = retain passphrase (default) */
} ehem_options;

/*
 * Initialize `opts` to library defaults and stamp abi_size. Call this before
 * overriding individual fields; it is the only supported way to prepare an
 * ehem_options for ehem_ctx_create(). No-op if `opts` is NULL.
 */
EHEM_API void ehem_options_init(ehem_options *opts);

/* ==========================================================================
 * Context lifecycle
 * implements: REQ-API-001, REQ-API-002
 *
 * ehem_ctx is opaque. One context == one HEM instance; there is no global
 * mutable state beyond the ehem_global_init/cleanup pair below. In 1.x a
 * context is used by one thread at a time (callers serialize).
 * ========================================================================== */
typedef struct ehem_ctx ehem_ctx;

/*
 * Create a context for the HEM at `url` (must be an http:// or https:// URL).
 * `opts` may be NULL for all defaults; otherwise it must have been prepared
 * with ehem_options_init(). On success writes *out and returns EHEM_OK. On
 * invalid arguments returns EHEM_ERR_ARG (and sets *out to NULL when out is
 * non-NULL); on allocation failure returns EHEM_ERR_NOMEM.
 */
EHEM_API ehem_rc ehem_ctx_create(const char *url,
                                 const ehem_options *opts,
                                 ehem_ctx **out);

/*
 * Destroy a context and release everything it owns, zeroizing credential
 * material. Passing NULL is a safe no-op.
 */
EHEM_API void ehem_ctx_destroy(ehem_ctx *ctx);

/* ==========================================================================
 * Last-error detail
 * implements: REQ-API-004
 * ========================================================================== */
typedef struct ehem_error {
    long        http_status;     /* HTTP status of the last call, or 0 if none */
    const char *device_payload;  /* device error body (NUL-terminated), or NULL */
    const char *message;         /* human-readable message; never NULL */
} ehem_error;

/*
 * Return detail about the most recent call on `ctx`. The returned struct and
 * its strings are owned by the context and remain valid only until the next
 * API call on the same context — copy anything that must outlive that. After a
 * successful call the detail is reset (http_status 0, device_payload NULL,
 * message ""). Returns NULL only if `ctx` is NULL.
 */
EHEM_API const ehem_error *ehem_last_error(const ehem_ctx *ctx);

/*
 * True once an automatic certificate recovery (expired device cert →
 * check-in → retry; see ehem_options.no_auto_checkin) has taken effect on
 * this context — i.e. the check-in completed AND the retried request then
 * verified against the device. Sticky for the context's lifetime, so a
 * consumer can inform the user after its operations completed. False for a
 * NULL ctx.
 */
EHEM_API int ehem_cert_refreshed(const ehem_ctx *ctx);

/* ==========================================================================
 * Process-global init / cleanup
 * implements: REQ-API-002
 *
 * The library keeps no mutable global state except this pair, which wraps the
 * transport's process-global initialization (libcurl's curl_global_init). Both
 * are idempotent: calling init or cleanup more than once is safe, and init may
 * be called again after cleanup. Calling ehem_global_init() once at program
 * start is recommended for deterministic setup but is not required — a context
 * initializes what it needs. Not thread-safe against concurrent context
 * creation (1.x single-thread-per-context model); call at startup.
 * ========================================================================== */
EHEM_API ehem_rc ehem_global_init(void);
EHEM_API void ehem_global_cleanup(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_H */
