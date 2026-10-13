/*
 * crypto_ref_openssl.c — crypto_ref.h on OpenSSL 3 (EHEM_CRYPTO_BACKEND=openssl).
 *
 * supports: REQ-BUILD-007
 */
#include "crypto_ref.h"

#include <limits.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

const char *ref_backend(void)
{
    return "OpenSSL";
}

int ref_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    unsigned int n = 0;
    return (EVP_Digest(in, len, out, &n, EVP_sha256(), NULL) == 1 && n == 32)
               ? 0 : -1;
}

int ref_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *out, size_t out_len)
{
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    EVP_KDF_CTX *ctx = (kdf != NULL) ? EVP_KDF_CTX_new(kdf) : NULL;
    OSSL_PARAM params[5];
    size_t n = 0;
    int ret = -1;

    params[n++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                                   (char *)"SHA256", 0);
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    (void *)ikm, ikm_len);
    if (salt != NULL && salt_len > 0) {   /* absent = HashLen zeros */
        params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                        (void *)salt, salt_len);
    }
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                    (void *)info, info_len);
    params[n] = OSSL_PARAM_construct_end();
    if (ctx != NULL && EVP_KDF_derive(ctx, out, out_len, params) == 1) {
        ret = 0;
    }
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    return ret;
}

static const EVP_CIPHER *gcm_for(size_t key_len)
{
    switch (key_len) {
    case 16: return EVP_aes_128_gcm();
    case 24: return EVP_aes_192_gcm();
    case 32: return EVP_aes_256_gcm();
    default: return NULL;
    }
}

int ref_aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                        const uint8_t *iv, size_t iv_len,
                        const uint8_t *pt, size_t pt_len,
                        const uint8_t *aad, size_t aad_len,
                        uint8_t *ct, uint8_t *tag, size_t tag_len)
{
    const EVP_CIPHER *cipher = gcm_for(key_len);
    EVP_CIPHER_CTX *ctx;
    int n = 0;
    int ret = -1;

    if (cipher == NULL || iv_len > INT_MAX || pt_len > INT_MAX ||
        aad_len > INT_MAX || tag_len > 16) {
        return -1;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (ctx != NULL &&
        EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) == 1 &&
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
        (aad_len == 0 ||
         EVP_EncryptUpdate(ctx, NULL, &n, aad, (int)aad_len) == 1) &&
        EVP_EncryptUpdate(ctx, ct, &n, pt, (int)pt_len) == 1 &&
        EVP_EncryptFinal_ex(ctx, ct + n, &n) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)tag_len, tag) == 1) {
        ret = 0;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ret;
}

static const EVP_CIPHER *wrap_for(size_t kek_len)
{
    switch (kek_len) {
    case 16: return EVP_aes_128_wrap();
    case 24: return EVP_aes_192_wrap();
    case 32: return EVP_aes_256_wrap();
    default: return NULL;
    }
}

int ref_aes_keywrap(const uint8_t *kek, size_t kek_len,
                    const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t out_cap, size_t *out_len)
{
    const EVP_CIPHER *cipher = wrap_for(kek_len);
    EVP_CIPHER_CTX *ctx;
    int n = 0;
    int fin = 0;
    int ret = -1;

    if (cipher == NULL || in_len > INT_MAX - 8 || out_cap < in_len + 8) {
        return -1;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (ctx != NULL) {
        /* The wrap modes refuse to run without this flag. */
        EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
        if (EVP_EncryptInit_ex(ctx, cipher, NULL, kek, NULL) == 1 &&
            EVP_EncryptUpdate(ctx, out, &n, in, (int)in_len) == 1 &&
            EVP_EncryptFinal_ex(ctx, out + n, &fin) == 1) {
            *out_len = (size_t)n + (size_t)fin;
            ret = 0;
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    return ret;
}
