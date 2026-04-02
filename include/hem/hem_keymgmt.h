#ifndef HEM_KEYMGMT_H
#define HEM_KEYMGMT_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * POST /api/keymgmt/create
 * Scope: keymgmt:gen  (authenticated automatically)
 *
 * label    key label, max 31 chars
 * type     key type string, e.g. "AES256", "CURVE25519", "ED25519"
 * kid_out  buffer receiving the 32-char hex key ID (needs >= 33 bytes)
 */
hem_error_t hem_key_create(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            char       *kid_out,
                            size_t      kid_size);

/*
 * DELETE /api/keymgmt/delete/{kid}
 * Scope: keymgmt:del  (authenticated automatically)
 */
hem_error_t hem_key_delete(hem_ctx_t *ctx, const char *kid);

/*
 * GET /api/keymgmt/list/{offset}/{limit}
 * Scope: keymgmt:list  (authenticated automatically)
 *
 * list      caller-allocated array of hem_key_info_t
 * list_cap  number of entries in list[]
 * total     receives total key count on the device
 * listed    receives number of entries written to list[]
 */
hem_error_t hem_key_list(hem_ctx_t      *ctx,
                          int             offset,
                          int             limit,
                          hem_key_info_t *list,
                          int             list_cap,
                          int            *total,
                          int            *listed);

/*
 * GET /api/keymgmt/get/{kid}
 * Scope: keymgmt:list  (authenticated automatically)
 * Fills *out with public key metadata.
 */
hem_error_t hem_key_get(hem_ctx_t      *ctx,
                         const char     *kid,
                         hem_key_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HEM_KEYMGMT_H */
