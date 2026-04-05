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
 * Fills *out with public key metadata including pubkey (base64).
 */
hem_error_t hem_key_get(hem_ctx_t      *ctx,
                         const char     *kid,
                         hem_key_info_t *out);

/*
 * POST /api/keymgmt/derive
 * Scope: keymgmt:gen  (authenticated automatically)
 *
 * Derives a new key via ECDH from an existing ECDH-capable key and a peer
 * public key.  Either ecdh_kid (key stored on device) or peer_pubkey_b64
 * (raw external public key in base64) must be non-NULL.
 *
 * label          key label for the derived key
 * type           derived key type, e.g. "AES256"
 * ecdh_kid       KID of the local ECDH private key
 * peer_pubkey_b64 peer public key (base64), or NULL if peer_kid used instead
 * kid_out        buffer receiving the derived key ID (>= 33 bytes)
 */
hem_error_t hem_key_derive(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            const char *ecdh_kid,
                            const char *peer_pubkey_b64,
                            char       *kid_out,
                            size_t      kid_size);

/*
 * POST /api/keymgmt/import
 * Scope: keymgmt:imp  (authenticated automatically)
 *
 * Imports an external public key into the key repository.
 *
 * label       key label
 * type        key type, e.g. "CURVE25519", "SECP256R1"
 * pubkey_b64  public key in base64
 * mode        optional usage mode: "ECDH", "ExDSA", or "ECDH,ExDSA"; may be NULL
 * kid_out     buffer receiving the new key ID (>= 33 bytes)
 */
hem_error_t hem_key_import(hem_ctx_t  *ctx,
                            const char *label,
                            const char *type,
                            const char *pubkey_b64,
                            const char *mode,
                            char       *kid_out,
                            size_t      kid_size);

/*
 * POST /api/keymgmt/update
 * Scope: keymgmt:upd  (authenticated automatically)
 *
 * Updates a key's label and/or description.  At least one of new_label or
 * new_descr_b64 must be non-NULL.
 *
 * kid           key to update
 * new_label     new label string, or NULL to leave unchanged
 * new_descr_b64 new base64-encoded binary descriptor, or NULL to leave unchanged
 */
hem_error_t hem_key_update(hem_ctx_t  *ctx,
                            const char *kid,
                            const char *new_label,
                            const char *new_descr_b64);

/*
 * POST /api/keymgmt/search
 * Scope: keymgmt:search  (authenticated automatically)
 *
 * Searches keys by their descr field.  descr_b64 is a base64-encoded pattern;
 * prefix the base64 string with '^' for starts-with matching.
 *
 * Returns HEM_ERR_HTTP_STATUS (404) if no keys match.
 */
hem_error_t hem_key_search(hem_ctx_t      *ctx,
                            const char     *descr_b64,
                            int             offset,
                            int             limit,
                            hem_key_info_t *list,
                            int             list_cap,
                            int            *total,
                            int            *listed);

#ifdef __cplusplus
}
#endif

#endif /* HEM_KEYMGMT_H */
