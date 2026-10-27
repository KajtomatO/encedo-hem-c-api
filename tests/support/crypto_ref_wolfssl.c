/*
 * crypto_ref_wolfssl.c — crypto_ref.h on wolfCrypt (EHEM_CRYPTO_BACKEND=wolfssl).
 *
 * supports: REQ-BUILD-007
 */
#include "crypto_ref.h"

#include <wolfssl/options.h>            /* build config — must precede wolfcrypt */
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/kdf.h>

const char *ref_backend(void)
{
    return "wolfSSL";
}

int ref_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    return wc_Sha256Hash(in, (word32)len, out) == 0 ? 0 : -1;
}

int ref_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *out, size_t out_len)
{
    return wc_HKDF(WC_SHA256, ikm, (word32)ikm_len, salt, (word32)salt_len,
                   info, (word32)info_len, out, (word32)out_len) == 0 ? 0 : -1;
}

int ref_aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                        const uint8_t *iv, size_t iv_len,
                        const uint8_t *pt, size_t pt_len,
                        const uint8_t *aad, size_t aad_len,
                        uint8_t *ct, uint8_t *tag, size_t tag_len)
{
    Aes aes;
    int ret;

    if (wc_AesInit(&aes, NULL, INVALID_DEVID) != 0) {
        return -1;
    }
    ret = wc_AesGcmSetKey(&aes, key, (word32)key_len);
    if (ret == 0) {
        ret = wc_AesGcmEncrypt(&aes, ct, pt, (word32)pt_len, iv, (word32)iv_len,
                               tag, (word32)tag_len, aad, (word32)aad_len);
    }
    wc_AesFree(&aes);
    return ret == 0 ? 0 : -1;
}

int ref_aes_keywrap(const uint8_t *kek, size_t kek_len,
                    const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t out_cap, size_t *out_len)
{
#ifdef HAVE_AES_KEYWRAP
    /* Returns the wrapped length on success. */
    int ret = wc_AesKeyWrap(kek, (word32)kek_len, in, (word32)in_len,
                            out, (word32)out_cap, NULL);
    if (ret <= 0) {
        return -1;
    }
    *out_len = (size_t)ret;
    return 0;
#else
    /* MSYS2's wolfSSL is built without HAVE_AES_KEYWRAP (wc_AesKeyWrap is not
     * even declared). */
    (void)kek;
    (void)kek_len;
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_cap;
    (void)out_len;
    return CRYPTO_REF_UNSUPPORTED;
#endif
}
