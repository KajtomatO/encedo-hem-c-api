/*
 * system.h — Encedo HEM C SDK, `system` protocol bindings.
 *
 * implements: REQ-SYS-001, REQ-SYS-002, REQ-API-005
 *
 * Unauthenticated device introspection: GET /api/system/status and
 * GET /api/system/version. Both return a typed struct the library allocates
 * and the caller releases with the matching ehem_*_free() (REQ-API-005).
 *
 * Field sets follow encedo-hem-api-doc system/status.md and system/version.md
 * (fetched 2026-07-15). Optional fields are marked present/absent: optional
 * strings are NULL when absent; optional numbers/booleans carry a `has_*`
 * flag (0/false being valid values). Unknown fields in the device response are
 * ignored (tolerant parsing); a missing REQUIRED field is EHEM_ERR_PROTOCOL.
 */
#ifndef EHEM_SYSTEM_H
#define EHEM_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Key-repository memory statistics (status.repo_stats; present only with a
 * valid token, so absent on the M1 unauthenticated call). */
typedef struct ehem_repo_stats {
    int64_t deleted;
    int64_t fragmentation;
    int64_t freespace;
    int64_t invalid;
    int64_t total;
} ehem_repo_stats;

/*
 * GET /api/system/status result (filled by ehem_system_status()). Required
 * fields (always present per the doc): ctx, fls_state, uptime, temp. Everything
 * else is optional. The struct is named `_info` so it does not collide with the
 * ehem_system_status() function (C has one namespace for both).
 */
typedef struct ehem_status_info {
    /* Required. */
    int64_t ctx;         /* context number */
    int64_t fls_state;   /* fail-state value (0 = no errors) */
    int64_t uptime;      /* seconds since boot (primary presence indicator) */
    double  temp;        /* chip temperature, Celsius */

    /* storage: per-disk status strings (doc: array; may be empty). */
    char  **storage;
    size_t  storage_count;

    /* Optional strings (NULL when absent). */
    char *ts;            /* current time, ISO 8601 — only if RTC is set */
    char *hostname;      /* device hostname */
    char *format;        /* disk formatting state during personalization */

    /* Optional number (Unix timestamp — only if RTC is set). */
    bool    has_time;
    int64_t time;

    /* Optional booleans. */
    bool has_fw_upgrade; bool fw_upgrade; /* successful reboot post-upgrade */
    bool has_inited;     bool inited;     /* false if device unpersonalized */
    bool has_https;      bool https;      /* HTTPS availability */
    bool has_tts;        bool tts;        /* false if TrustedTime disabled */

    /* Optional nested repo stats (requires auth). */
    bool            has_repo_stats;
    ehem_repo_stats repo_stats;
} ehem_status_info;

/*
 * GET /api/system/version result (filled by ehem_system_version()). Required
 * fields: hwv, fwv, blv. The signing blobs and the rest are optional strings
 * (NULL when absent). Named `_info` to avoid colliding with the function.
 */
typedef struct ehem_version_info {
    /* Required. */
    char *hwv;   /* hardware version, e.g. "PPA rev 2.2" */
    char *fwv;   /* firmware version */
    char *blv;   /* bootloader version */

    /* Optional signing key/signature blobs (base64). */
    char *fwk;   /* firmware signing public key */
    char *fws;   /* firmware signature */
    char *blk;   /* bootloader signing public key */
    char *bls;   /* bootloader signature */

    /* Optional. */
    char *uis;     /* Encedo Manager version hash */
    char *sd_csd;  /* microSD CSD (requires token) */
    char *sd_cid;  /* microSD CID (requires token) */
} ehem_version_info;

/*
 * Fetch device status. On success writes *out (caller frees with
 * ehem_system_status_free) and returns EHEM_OK. On failure returns an ehem_rc
 * and leaves *out NULL; detail is retrievable via ehem_last_error(ctx).
 */
EHEM_API ehem_rc ehem_system_status(ehem_ctx *ctx, ehem_status_info **out);

/* Release a status struct from ehem_system_status(). NULL is a no-op. */
EHEM_API void ehem_system_status_free(ehem_status_info *status);

/*
 * Fetch device version info. On success writes *out (caller frees with
 * ehem_system_version_free) and returns EHEM_OK; otherwise returns an ehem_rc,
 * leaves *out NULL, and records detail on the context.
 */
EHEM_API ehem_rc ehem_system_version(ehem_ctx *ctx, ehem_version_info **out);

/* Release a version struct from ehem_system_version(). NULL is a no-op. */
EHEM_API void ehem_system_version_free(ehem_version_info *version);

/*
 * Result of the check-in handshake (filled by ehem_system_checkin()). The
 * device reports what the cloud response delivered; all fields are optional
 * strings (NULL when absent — tolerant parsing, firmware variants differ).
 */
typedef struct ehem_checkin_info {
    char *status;   /* general check-in status */
    char *newcrt;   /* TLS certificate update status */
    char *newfws;   /* available firmware update info */
    char *newuis;   /* available Manager UI update info */
    bool  cert_updated;  /* derived: newcrt present and non-empty */
} ehem_checkin_info;

/*
 * Run the three-step check-in handshake — how the device verifies firmware,
 * sets its RTC, and refreshes its TLS certificate:
 *   1. GET /api/system/checkin on the device (challenge),
 *   2. POST the challenge to the Encedo cloud (ehem_options.checkin_url,
 *      default EHEM_DEFAULT_CHECKIN_URL; always TLS-verified),
 *   3. POST the cloud's verified response back to the device.
 * No authentication required. Run it at session start, after long offline
 * periods, or when the device clock/certificate is stale; the SDK also runs
 * it automatically on an expired-certificate failure unless
 * ehem_options.no_auto_checkin is set.
 *
 * On success writes *out (caller frees with ehem_checkin_result_free) and
 * returns EHEM_OK; otherwise returns an ehem_rc, leaves *out NULL, and
 * records detail on the context.
 */
EHEM_API ehem_rc ehem_system_checkin(ehem_ctx *ctx, ehem_checkin_info **out);

/* Release a check-in result from ehem_system_checkin(). NULL is a no-op. */
EHEM_API void ehem_checkin_result_free(ehem_checkin_info *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_SYSTEM_H */
