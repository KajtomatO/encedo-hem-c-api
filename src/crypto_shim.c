/*
 * crypto_shim.c — wolfCrypt-backed implementation of the auth primitives.
 *
 * implements: REQ-AUTH-001
 *
 * The wolfSSL headers are included ONLY here; the shim's public surface
 * (crypto_shim.h) is byte buffers, so wolfSSL never leaks into a shipped header
 * or the export table. X25519 uses EC25519_LITTLE_ENDIAN so scalars/points
 * match RFC 7748 byte order (verified against RFC 7748 §5.2/§6.1 in
 * tests/unit/test_crypto.c).
 */
#include "crypto_shim.h"

#include <string.h>

#include <wolfssl/options.h>            /* build config — must precede wolfcrypt */
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/curve25519.h>

ehem_rc ehem_kdf_pbkdf2_sha256(const uint8_t *passwd, size_t passwd_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len)
{
    if (passwd == NULL || salt == NULL || out == NULL ||
        out_len == 0 || iterations == 0) {
        return EHEM_ERR_ARG;
    }
    int rc = wc_PBKDF2(out, passwd, (int)passwd_len, salt, (int)salt_len,
                       (int)iterations, (int)out_len, WC_SHA256);
    return rc == 0 ? EHEM_OK : EHEM_ERR_PROTOCOL;
}

ehem_rc ehem_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[EHEM_SHA256_SIZE])
{
    if (key == NULL || out == NULL || (msg == NULL && msg_len != 0)) {
        return EHEM_ERR_ARG;
    }

    Hmac hmac;
    if (wc_HmacInit(&hmac, NULL, INVALID_DEVID) != 0) {
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    if (wc_HmacSetKey(&hmac, WC_SHA256, key, (word32)key_len) == 0 &&
        wc_HmacUpdate(&hmac, msg, (word32)msg_len) == 0 &&
        wc_HmacFinal(&hmac, out) == 0) {
        result = EHEM_OK;
    }
    wc_HmacFree(&hmac);
    return result;
}

ehem_rc ehem_x25519_keypair_from_seed(const uint8_t seed[EHEM_X25519_KEYSIZE],
                                      uint8_t priv_out[EHEM_X25519_KEYSIZE],
                                      uint8_t pub_out[EHEM_X25519_KEYSIZE])
{
    if (seed == NULL) {
        return EHEM_ERR_ARG;
    }

    /* RFC 7748 clamp of the little-endian scalar. */
    uint8_t priv[EHEM_X25519_KEYSIZE];
    memcpy(priv, seed, EHEM_X25519_KEYSIZE);
    priv[0]  &= 248;
    priv[31] &= 127;
    priv[31] |= 64;

    curve25519_key key;
    if (wc_curve25519_init(&key) != 0) {
        ehem_zeroize(priv, sizeof priv);
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    if (wc_curve25519_import_private_ex(priv, EHEM_X25519_KEYSIZE, &key,
                                        EC25519_LITTLE_ENDIAN) == 0) {
        result = EHEM_OK;
        if (pub_out != NULL) {
            word32 publen = EHEM_X25519_KEYSIZE;
            if (wc_curve25519_export_public_ex(&key, pub_out, &publen,
                                               EC25519_LITTLE_ENDIAN) != 0 ||
                publen != EHEM_X25519_KEYSIZE) {
                result = EHEM_ERR_PROTOCOL;
            }
        }
    }
    if (result == EHEM_OK && priv_out != NULL) {
        memcpy(priv_out, priv, EHEM_X25519_KEYSIZE);
    }

    wc_curve25519_free(&key);
    ehem_zeroize(priv, sizeof priv);
    return result;
}

ehem_rc ehem_x25519_shared(const uint8_t priv[EHEM_X25519_KEYSIZE],
                           const uint8_t peer_pub[EHEM_X25519_KEYSIZE],
                           uint8_t out[EHEM_X25519_KEYSIZE])
{
    if (priv == NULL || peer_pub == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }

    curve25519_key mine, peer;
    if (wc_curve25519_init(&mine) != 0) {
        return EHEM_ERR_PROTOCOL;
    }
    if (wc_curve25519_init(&peer) != 0) {
        wc_curve25519_free(&mine);
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    word32 outlen = EHEM_X25519_KEYSIZE;
    if (wc_curve25519_import_private_ex(priv, EHEM_X25519_KEYSIZE, &mine,
                                        EC25519_LITTLE_ENDIAN) == 0 &&
        wc_curve25519_import_public_ex(peer_pub, EHEM_X25519_KEYSIZE, &peer,
                                       EC25519_LITTLE_ENDIAN) == 0 &&
        wc_curve25519_shared_secret_ex(&mine, &peer, out, &outlen,
                                       EC25519_LITTLE_ENDIAN) == 0 &&
        outlen == EHEM_X25519_KEYSIZE) {
        result = EHEM_OK;
    }

    wc_curve25519_free(&peer);
    wc_curve25519_free(&mine);
    return result;
}

void ehem_zeroize(void *p, size_t n)
{
    if (p == NULL) {
        return;
    }
    /* volatile write loop: the compiler may not treat these stores as dead,
     * so the scrub survives -O2 (proven in test_crypto.c via a volatile read). */
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n-- > 0) {
        *vp++ = 0;
    }
}
