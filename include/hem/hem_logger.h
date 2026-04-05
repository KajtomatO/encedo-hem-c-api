#ifndef HEM_LOGGER_H
#define HEM_LOGGER_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Response from GET /api/logger/key */
typedef struct {
    char key_b64[128];         /* ED25519 public key (base64) */
    char nonce_b64[128];       /* Random nonce (base64) */
    char nonce_signed_b64[256];/* ED25519 signature of nonce (base64) */
} hem_logger_key_t;

/*
 * GET /api/logger/key
 * Scope: logger:get  (authenticated automatically)
 *
 * Returns the ED25519 public key used to sign audit log entries, plus a
 * freshly signed nonce for device authenticity verification.
 */
hem_error_t hem_logger_key(hem_ctx_t *ctx, hem_logger_key_t *out);

/*
 * GET /api/logger/list/{offset}
 * Scope: logger:get  (authenticated automatically)
 *
 * Lists available log file IDs (PPA only).
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 *
 * ids_out   caller-allocated array to receive integer log file IDs
 * ids_cap   capacity of ids_out
 * count     receives the number of IDs written
 */
hem_error_t hem_logger_list(hem_ctx_t *ctx, int offset,
                             int *ids_out, int ids_cap, int *count);

/*
 * GET /api/logger/{id}
 * Scope: logger:get  (authenticated automatically)
 *
 * Downloads a specific log file as plain text (PPA only).
 * Each entry is pipe-delimited; field names are not documented (see OQ-8).
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 *
 * buf      caller buffer for the plain-text log content
 * buf_size size of buf
 * out_len  receives the number of bytes written (excluding NUL)
 */
hem_error_t hem_logger_download(hem_ctx_t *ctx, int log_id,
                                 char *buf, size_t buf_size, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* HEM_LOGGER_H */
