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

/*
 * out = X25519(scalar, u): the raw Montgomery-ladder scalar multiplication,
 * done through import_private + import_public + shared_secret (all _ex with
 * EC25519_LITTLE_ENDIAN). Both the keypair (u = base point) and the ECDH
 * (u = peer public) go through this ONE path deliberately: it avoids
 * wc_curve25519_export_public_ex, whose "compute the public on export"
 * behaviour differs across wolfSSL builds (the MSYS2/MinGW package failed it
 * while it works on Debian). If X25519 works at all on a platform, ECDH works,
 * and so does this. Verified against RFC 7748 §5.2/§6.1.
 */
static ehem_rc x25519_scalarmult(const uint8_t scalar[EHEM_X25519_KEYSIZE],
                                 const uint8_t u[EHEM_X25519_KEYSIZE],
                                 uint8_t out[EHEM_X25519_KEYSIZE])
{
    curve25519_key mine, point;
    if (wc_curve25519_init(&mine) != 0) {
        return EHEM_ERR_PROTOCOL;
    }
    if (wc_curve25519_init(&point) != 0) {
        wc_curve25519_free(&mine);
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    word32 outlen = EHEM_X25519_KEYSIZE;
    if (wc_curve25519_import_private_ex(scalar, EHEM_X25519_KEYSIZE, &mine,
                                        EC25519_LITTLE_ENDIAN) == 0 &&
        wc_curve25519_import_public_ex(u, EHEM_X25519_KEYSIZE, &point,
                                       EC25519_LITTLE_ENDIAN) == 0 &&
        wc_curve25519_shared_secret_ex(&mine, &point, out, &outlen,
                                       EC25519_LITTLE_ENDIAN) == 0 &&
        outlen == EHEM_X25519_KEYSIZE) {
        result = EHEM_OK;
    }

    wc_curve25519_free(&point);
    wc_curve25519_free(&mine);
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

    /* public key = scalarmult(priv, base point u=9). */
    ehem_rc result = EHEM_OK;
    if (pub_out != NULL) {
        static const uint8_t basepoint[EHEM_X25519_KEYSIZE] = {9};
        result = x25519_scalarmult(priv, basepoint, pub_out);
    }
    if (result == EHEM_OK && priv_out != NULL) {
        memcpy(priv_out, priv, EHEM_X25519_KEYSIZE);
    }

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
    return x25519_scalarmult(priv, peer_pub, out);
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
