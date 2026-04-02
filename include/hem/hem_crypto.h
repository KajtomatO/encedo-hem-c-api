#ifndef HEM_CRYPTO_H
#define HEM_CRYPTO_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * POST /api/crypto/cipher/encrypt
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * kid         AES key ID
 * alg         algorithm string: "AES128-GCM", "AES256-GCM",
 *             "AES128-CBC", "AES256-CBC", "AES256-ECB", etc.
 * plaintext   raw plaintext bytes
 * pt_len      plaintext length in bytes (max ~5 KB -- device limit)
 * aad         optional additional authenticated data (GCM only), may be NULL
 * aad_len     length of aad (0 if aad is NULL)
 * ct_buf      caller-provided buffer to receive ciphertext
 * ct_buf_size size of ct_buf (must be >= pt_len for GCM/CBC)
 * result      filled with ciphertext pointer/length, IV, and GCM tag
 *
 * On success result->ciphertext == ct_buf and result->ciphertext_len is set.
 */
hem_error_t hem_encrypt(hem_ctx_t           *ctx,
                         const char          *kid,
                         const char          *alg,
                         const uint8_t       *plaintext,
                         size_t               pt_len,
                         const uint8_t       *aad,
                         size_t               aad_len,
                         uint8_t             *ct_buf,
                         size_t               ct_buf_size,
                         hem_cipher_result_t *result);

/*
 * POST /api/crypto/cipher/decrypt
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * kid          AES key ID
 * alg          same algorithm used for encryption
 * ciphertext   raw ciphertext bytes
 * ct_len       ciphertext length
 * iv / iv_len  initialization vector from encryption result
 * tag / tag_len GCM auth tag from encryption result (NULL/0 for non-GCM)
 * aad          optional AAD (must match what was used during encrypt)
 * aad_len      length of aad
 * pt_buf       caller-provided buffer for plaintext output
 * pt_buf_size  size of pt_buf
 * pt_out_len   receives actual plaintext length
 */
hem_error_t hem_decrypt(hem_ctx_t     *ctx,
                         const char    *kid,
                         const char    *alg,
                         const uint8_t *ciphertext,
                         size_t         ct_len,
                         const uint8_t *iv,
                         size_t         iv_len,
                         const uint8_t *tag,
                         size_t         tag_len,
                         const uint8_t *aad,
                         size_t         aad_len,
                         uint8_t       *pt_buf,
                         size_t         pt_buf_size,
                         size_t        *pt_out_len);

#ifdef __cplusplus
}
#endif

#endif /* HEM_CRYPTO_H */
