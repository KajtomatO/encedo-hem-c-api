/*
 * <ehem/logger.h> — audit-log read access: the log-signing key, the log-file
 * listing, and raw log-file download.
 *
 * The device keeps a signed, chain-HMAC'd audit log on its microSD (PPA
 * builds). Every record carries an Ed25519 signature by the device's
 * log-signing key (`Session.EIDkeySign` — distinct from the auth `eid`
 * Curve25519 key); ehem_logger_key() returns that public key together with a
 * freshly signed nonce proving the device holds the private half. Listing
 * and download are PPA-only routes: an EPA build has no microSD and answers
 * 404 → EHEM_ERR_NOT_FOUND.
 *
 * The documented `DELETE /api/logger/{id}` is deliberately NOT bound: its
 * dispatch is commented out of fw v1.2.2 (every request 404s), so there is
 * nothing to call — see REQ-SYS-009 (re-swept if firmware re-enables it).
 *
 * implements: REQ-SYS-009
 */
#ifndef EHEM_LOGGER_H
#define EHEM_LOGGER_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sizes of the decoded ehem_logger_key() material. */
#define EHEM_LOGGER_KEY_SIZE   32   /* Ed25519 public key */
#define EHEM_LOGGER_NONCE_SIZE 32   /* time-based nonce */
#define EHEM_LOGGER_SIG_SIZE   64   /* Ed25519 signature */

/*
 * Result of ehem_logger_key(): the audit-log signing identity. `nonce_signed`
 * is the Ed25519 signature over the raw `nonce` bytes by the private half of
 * `key` — verify it to confirm you are talking to the device that signs the
 * logs. The key is per-device (stable across reboots; rotated by a device
 * wipe/re-init, after which older logs no longer verify).
 */
typedef struct ehem_logger_key_info {
    uint8_t key[EHEM_LOGGER_KEY_SIZE];
    uint8_t nonce[EHEM_LOGGER_NONCE_SIZE];
    uint8_t nonce_signed[EHEM_LOGGER_SIG_SIZE];
} ehem_logger_key_info;

/*
 * Fetch the log-signing key + signed nonce: GET /api/logger/key (scope
 * "logger:get"; available on BOTH build variants). On success writes *out
 * (caller frees with ehem_logger_key_free) and returns EHEM_OK. A field that
 * does not base64-decode to its exact size is EHEM_ERR_PROTOCOL.
 */
EHEM_API ehem_rc ehem_logger_key(ehem_ctx *ctx, ehem_logger_key_info **out);

/* Release a key info. NULL is a no-op. */
EHEM_API void ehem_logger_key_free(ehem_logger_key_info *info);

/*
 * One page of the audit-log file listing (ehem_logger_list). `ids` are the
 * hex file ids as the device names them (creation-timestamp derived —
 * lexicographic order is chronological). The device controls the page size;
 * advance `offset` by `count` until it reaches `total`.
 */
typedef struct ehem_logger_page {
    int64_t  total;   /* total log files on the device */
    size_t   count;   /* ids in this page */
    char   **ids;     /* `count` NUL-terminated ids; NULL when count == 0 */
} ehem_logger_page;

/*
 * List audit-log file ids: GET /api/logger/list[/{offset}] (scope
 * "logger:get"; PPA-only — EPA 404 → EHEM_ERR_NOT_FOUND). On success writes
 * *out (caller frees with ehem_logger_page_free) and returns EHEM_OK. A
 * device listing failure is 404 → EHEM_ERR_NOT_FOUND; a malformed offset is
 * 406 → EHEM_ERR_DEVICE.
 */
EHEM_API ehem_rc ehem_logger_list(ehem_ctx *ctx, size_t offset,
                                  ehem_logger_page **out);

/* Release a listing page. NULL is a no-op. */
EHEM_API void ehem_logger_page_free(ehem_logger_page *page);

/*
 * Download one audit-log file as raw text: GET /api/logger/{id} (scope
 * "logger:get"; PPA-only). The body is returned VERBATIM — the SDK does not
 * parse it. Observed live format (fw v1.2.2 — the API doc's "JSON-like
 * record per line" is wrong): a "# Encedo nGINE FW <version>" header line,
 * then one PIPE-DELIMITED record per CRLF line
 * ("seq|ts|type|result|…|signature|chain", base64url-encoded fields carrying
 * the per-record Ed25519 signature and the HMAC chain link). On success writes a
 * NUL-terminated caller-owned buffer into *data and its text length into
 * *len (log files are line-oriented text; an embedded NUL would end the
 * reported length early). Free with ehem_logger_file_free. An unknown id is
 * 404 → EHEM_ERR_NOT_FOUND; a
 * file locked by another operation is 406 → EHEM_ERR_DEVICE. A body shorter
 * than the declared Content-Length (device-side I/O error mid-stream) is
 * reported by the transport as a failed request.
 */
EHEM_API ehem_rc ehem_logger_get(ehem_ctx *ctx, const char *id,
                                 uint8_t **data, size_t *len);

/* Release a downloaded log file. NULL is a no-op. */
EHEM_API void ehem_logger_file_free(uint8_t *data);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_LOGGER_H */
