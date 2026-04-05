#ifndef HEM_PQC_H
#define HEM_PQC_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * POST /api/crypto/pqc/mlkem/encaps
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Performs ML-KEM (FIPS 203) encapsulation.  The key must be of type
 * MLKEM512, MLKEM768, or MLKEM1024.
 *
 * kid          ML-KEM public key ID
 * ss_out       caller buffer for shared secret (32 bytes for all variants)
 * ss_size      size of ss_out (>= 32)
 * ss_len       receives actual shared secret length
 * ct_out       caller buffer for ciphertext
 *              (MLKEM512: 768 B, MLKEM768: 1088 B, MLKEM1024: 1568 B)
 * ct_size      size of ct_out
 * ct_len       receives actual ciphertext length
 */
hem_error_t hem_mlkem_encaps(hem_ctx_t *ctx,
                              const char *kid,
                              uint8_t    *ss_out,
                              size_t      ss_size,
                              size_t     *ss_len,
                              uint8_t    *ct_out,
                              size_t      ct_size,
                              size_t     *ct_len);

/*
 * POST /api/crypto/pqc/mlkem/decaps
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Performs ML-KEM decapsulation.  The shared secret returned must match the
 * one from the corresponding encapsulation.
 *
 * ciphertext   ciphertext from hem_mlkem_encaps
 * ct_len       length of ciphertext
 * ss_out       caller buffer for shared secret (32 bytes)
 * ss_size      size of ss_out
 * ss_len       receives actual shared secret length
 */
hem_error_t hem_mlkem_decaps(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *ciphertext,
                              size_t         ct_len,
                              uint8_t       *ss_out,
                              size_t         ss_size,
                              size_t        *ss_len);

/*
 * POST /api/crypto/pqc/mldsa/sign
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Creates a post-quantum digital signature (FIPS 204 ML-DSA).
 * Key types: MLDSA44, MLDSA65, MLDSA87.
 * Signature sizes: 2420 (MLDSA44), 3309 (MLDSA65), 4627 (MLDSA87).
 *
 * sig_out      caller buffer for signature (>= 4627 bytes to support all variants)
 * sig_size     size of sig_out
 * sig_len      receives actual signature length
 */
hem_error_t hem_mldsa_sign(hem_ctx_t     *ctx,
                            const char    *kid,
                            const uint8_t *msg,
                            size_t         msg_len,
                            uint8_t       *sig_out,
                            size_t         sig_size,
                            size_t        *sig_len);

/*
 * POST /api/crypto/pqc/mldsa/verify
 * Scope: keymgmt:use:<kid>  (authenticated automatically)
 *
 * Verifies an ML-DSA signature.  Returns HEM_OK if valid, error otherwise.
 */
hem_error_t hem_mldsa_verify(hem_ctx_t     *ctx,
                              const char    *kid,
                              const uint8_t *msg,
                              size_t         msg_len,
                              const uint8_t *sig,
                              size_t         sig_len);

#ifdef __cplusplus
}
#endif

#endif /* HEM_PQC_H */
