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
 * out = X25519(scalar, u): the raw Montgomery-ladder scalar multiplication.
 *
 * Implemented with wc_curve25519_generic, which takes ONLY byte buffers — no
 * curve25519_key struct crosses the wolfSSL ABI boundary. That is deliberate
 * and load-bearing: the prebuilt MSYS2/MinGW wolfSSL (5.9.2) lays out
 * curve25519_key at 128 bytes while the Debian build (5.6.6) uses 112, so
 * handing a caller-allocated key to the DLL corrupted the stack and crashed
 * (EXCEPTION_ACCESS_VIOLATION on CI). Passing only 32-byte arrays sidesteps the
 * layout mismatch entirely. Bytes are little-endian (RFC 7748); verified
 * against RFC 7748 §6.1 keypair + Diffie-Hellman. Both the keypair
 * (u = base point) and the ECDH (u = peer public) go through this one path.
 *
 * generic validates u as a real public key and rejects e.g. small-order /
 * non-canonical points (→ EHEM_ERR_PROTOCOL); the device's spk is always a
 * valid key, so this only ever fires on a malformed/hostile peer key.
 */
static ehem_rc x25519_scalarmult(const uint8_t scalar[EHEM_X25519_KEYSIZE],
                                 const uint8_t u[EHEM_X25519_KEYSIZE],
                                 uint8_t out[EHEM_X25519_KEYSIZE])
{
    /* wc_curve25519_generic requires an already-clamped scalar (it rejects an
     * unclamped one with ECC_BAD_ARG_E, unlike the struct API which clamped
     * internally). Clamp a local copy so callers may pass a raw seed. */
    uint8_t k[EHEM_X25519_KEYSIZE];
    memcpy(k, scalar, EHEM_X25519_KEYSIZE);
    k[0]  &= 248;
    k[31] &= 127;
    k[31] |= 64;

    int rc = wc_curve25519_generic(EHEM_X25519_KEYSIZE, out,
                                   EHEM_X25519_KEYSIZE, k,
                                   EHEM_X25519_KEYSIZE, u);
    ehem_zeroize(k, sizeof k);
    return rc == 0 ? EHEM_OK : EHEM_ERR_PROTOCOL;
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
