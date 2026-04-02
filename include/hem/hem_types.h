#ifndef HEM_TYPES_H
#define HEM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Opaque context handle */
typedef struct hem_ctx hem_ctx_t;

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
typedef enum {
    HEM_OK               = 0,
    HEM_ERR_INVALID_ARG,      /* NULL pointer or bad parameter */
    HEM_ERR_HTTP,             /* libcurl transport error */
    HEM_ERR_HTTP_STATUS,      /* Non-200 HTTP response */
    HEM_ERR_JSON,             /* Malformed or unexpected JSON */
    HEM_ERR_AUTH,             /* Authentication failed (401/403) */
    HEM_ERR_DEVICE_FAILURE,   /* Device in FLS failure state (409) */
    HEM_ERR_BUFFER_TOO_SMALL, /* Caller output buffer too small */
    HEM_ERR_OPENSSL,          /* OpenSSL operation failed */
    HEM_ERR_CHECKIN,          /* Check-in backend unreachable or failed */
} hem_error_t;

/* -------------------------------------------------------------------------
 * Role for authentication
 * ---------------------------------------------------------------------- */
typedef enum {
    HEM_ROLE_USER   = 0,
    HEM_ROLE_MASTER = 1,
} hem_role_t;

/* -------------------------------------------------------------------------
 * System structs
 * ---------------------------------------------------------------------- */

/* Response from GET /api/system/version */
typedef struct {
    char hwv[32];   /* Hardware version */
    char blv[32];   /* Bootloader version */
    char fwv[32];   /* Firmware version */
    char fws[65];   /* Firmware signature (hex) */
    char uis[65];   /* UI signature (PPA only) */
} hem_version_t;

/* Response from GET /api/system/status */
typedef struct {
    int     fls_state;      /* Failure state bitmask; 0 = no errors */
    int64_t ts;             /* RTC timestamp; -1 if absent */
    char    hostname[128];  /* Configured FQDN */
    bool    https;          /* TLS operational */
    bool    initialized;    /* true = device is initialized */
    int     uptime;         /* Seconds since boot */
    int     temp;           /* Temperature in Celsius */
} hem_status_t;

/* Response from GET /api/system/config (requires system:config scope) */
typedef struct {
    char    eid[128];       /* Device entity ID */
    char    user[128];      /* Configured user name */
    char    email[128];     /* Configured email */
    char    hostname[128];  /* Device FQDN */
    int64_t uts;            /* Last config update timestamp */
} hem_config_t;

/* -------------------------------------------------------------------------
 * Key management structs
 * ---------------------------------------------------------------------- */

/* Single key list entry from GET /api/keymgmt/list */
typedef struct {
    char    kid[33];        /* 32-char hex key ID + NUL */
    char    label[32];      /* Key label */
    char    type[64];       /* Comma-separated type attributes */
    int64_t created;        /* Creation timestamp */
    int64_t updated;        /* Last update timestamp */
} hem_key_info_t;

/* -------------------------------------------------------------------------
 * Crypto structs
 * ---------------------------------------------------------------------- */

/* Result from hem_encrypt() -- buffers owned by caller */
typedef struct {
    uint8_t *ciphertext;        /* Pointer into caller-provided buffer */
    size_t   ciphertext_len;
    uint8_t  iv[16];
    size_t   iv_len;
    uint8_t  tag[16];           /* GCM auth tag */
    size_t   tag_len;
} hem_cipher_result_t;

#endif /* HEM_TYPES_H */
