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

/*
 * POST /api/crypto/cipher/wrap
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Wraps (encrypts) key material using AES Key Wrap (RFC 3394).
 *
 * kid              AES wrapping key ID
 * alg              "AES128", "AES192", or "AES256"
 * key_material     raw bytes to wrap (must be multiple of 8, minimum 16 bytes)
 * key_len          length of key_material
 * wrapped_out      caller buffer for wrapped output (key_len + 8 bytes)
 * wrapped_size     size of wrapped_out
 * wrapped_len      receives actual wrapped length
 */
hem_error_t hem_key_wrap(hem_ctx_t     *ctx,
                          const char    *kid,
                          const char    *alg,
                          const uint8_t *key_material,
                          size_t         key_len,
                          uint8_t       *wrapped_out,
                          size_t         wrapped_size,
                          size_t        *wrapped_len);

/*
 * POST /api/crypto/cipher/unwrap
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Unwraps (decrypts) wrapped key material.
 *
 * wrapped      wrapped key bytes from hem_key_wrap
 * wrapped_len  length of wrapped
 * key_out      caller buffer for unwrapped key material
 * key_size     size of key_out
 * key_len      receives actual unwrapped length
 */
hem_error_t hem_key_unwrap(hem_ctx_t     *ctx,
                            const char    *kid,
                            const char    *alg,
                            const uint8_t *wrapped,
                            size_t         wrapped_len,
                            uint8_t       *key_out,
                            size_t         key_size,
                            size_t        *key_len);

/*
 * POST /api/crypto/hmac/hash
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Computes HMAC over a message.
 *
 * kid      HMAC key ID (or ECDH key ID when ext_kid/pubkey supplied)
 * alg      hash algorithm, e.g. "SHA2-256"; may be NULL for direct HMAC keys
 * msg      raw message bytes (max 2048 bytes)
 * msg_len  length of msg
 * mac_out  caller buffer for HMAC output
 * mac_size size of mac_out
 * mac_len  receives actual HMAC length
 */
hem_error_t hem_hmac_hash(hem_ctx_t     *ctx,
                           const char    *kid,
                           const char    *alg,
                           const uint8_t *msg,
                           size_t         msg_len,
                           uint8_t       *mac_out,
                           size_t         mac_size,
                           size_t        *mac_len);

/*
 * POST /api/crypto/hmac/verify
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Verifies an HMAC.  Returns HEM_OK if valid, HEM_ERR_AUTH if invalid.
 */
hem_error_t hem_hmac_verify(hem_ctx_t     *ctx,
                             const char    *kid,
                             const char    *alg,
                             const uint8_t *msg,
                             size_t         msg_len,
                             const uint8_t *mac,
                             size_t         mac_len);

/*
 * POST /api/crypto/exdsa/sign
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Creates a digital signature (ECDSA or EdDSA).
 *
 * kid          signing key ID
 * alg          algorithm string, e.g. "Ed25519", "SHA256WithECDSA"
 * msg / msg_len message to sign
 * sign_ctx / sign_ctx_len optional context for Ed25519ctx/Ed448 (NULL/0 for most)
 * sig_out      caller buffer for signature
 * sig_size     size of sig_out
 * sig_len      receives actual signature length
 */
hem_error_t hem_sign(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *alg,
                      const uint8_t *msg,
                      size_t         msg_len,
                      const uint8_t *sign_ctx,
                      size_t         sign_ctx_len,
                      uint8_t       *sig_out,
                      size_t         sig_size,
                      size_t        *sig_len);

/*
 * POST /api/crypto/exdsa/verify
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Verifies a digital signature.  Returns HEM_OK if valid, error otherwise.
 */
hem_error_t hem_verify(hem_ctx_t     *ctx,
                        const char    *kid,
                        const char    *alg,
                        const uint8_t *msg,
                        size_t         msg_len,
                        const uint8_t *sig,
                        size_t         sig_len,
                        const uint8_t *sign_ctx,
                        size_t         sign_ctx_len);

/*
 * POST /api/crypto/ecdh
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Performs ECDH key agreement.  Exactly one of peer_pubkey_b64 or peer_kid
 * must be non-NULL.
 *
 * kid              local private key ID
 * peer_pubkey_b64  peer public key (base64), or NULL to use peer_kid
 * peer_kid         KID of imported peer public key, or NULL to use peer_pubkey_b64
 * alg              optional hash algorithm for output, e.g. "SHA2-256";
 *                  NULL for raw ECDH shared secret
 * secret_out       caller buffer for shared secret
 * secret_size      size of secret_out
 * secret_len       receives actual secret length
 */
hem_error_t hem_ecdh(hem_ctx_t     *ctx,
                      const char    *kid,
                      const char    *peer_pubkey_b64,
                      const char    *peer_kid,
                      const char    *alg,
                      uint8_t       *secret_out,
                      size_t         secret_size,
                      size_t        *secret_len);

#ifdef __cplusplus
}
#endif

#endif /* HEM_CRYPTO_H */
