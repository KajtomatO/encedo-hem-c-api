/*
 * crypto_ref.h — reference computations for the live tests: what the device
 * computes (HKDF, AES-GCM, AES key wrap, SHA-256), reproduced with the build's
 * crypto library directly — not through the SDK's crypto shim, so a local
 * result is an independent check of the device's. One implementation per
 * backend (crypto_ref_wolfssl.c / crypto_ref_openssl.c, EHEM_CRYPTO_BACKEND);
 * they are the only files under tests/ that include a crypto library header
 * (the tests_backend_free gate).
 *
 * supports: REQ-BUILD-007
 *
 * Every function returns 0 on success, CRYPTO_REF_UNSUPPORTED when this build
 * of the library lacks the primitive (MSYS2's wolfSSL has no AES key wrap),
 * -1 on any other failure.
 */
#ifndef EHEM_CRYPTO_REF_H
#define EHEM_CRYPTO_REF_H

#include <stddef.h>
#include <stdint.h>

#define CRYPTO_REF_UNSUPPORTED 1

/* The library behind these functions, for probe messages ("wolfSSL"). */
const char *ref_backend(void);

int ref_sha256(const uint8_t *in, size_t len, uint8_t out[32]);

/* HKDF-SHA256 (RFC 5869); salt may be NULL / 0 (= HashLen zeros). */
int ref_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *out, size_t out_len);

/* AES-GCM encryption, key 16/24/32 bytes, any IV length, optional AAD;
 * writes pt_len bytes of ciphertext to `ct` and `tag_len` bytes to `tag`. */
int ref_aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                        const uint8_t *iv, size_t iv_len,
                        const uint8_t *pt, size_t pt_len,
                        const uint8_t *aad, size_t aad_len,
                        uint8_t *ct, uint8_t *tag, size_t tag_len);

/* AES key wrap (RFC 3394, default IV), KEK 16/24/32 bytes: in_len + 8 bytes
 * into `out` (capacity out_cap); *out_len set on success. */
int ref_aes_keywrap(const uint8_t *kek, size_t kek_len,
                    const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t out_cap, size_t *out_len);

#endif /* EHEM_CRYPTO_REF_H */
