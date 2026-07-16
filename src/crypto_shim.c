/*
 * crypto_shim.c — wolfCrypt-backed implementation of the auth primitives.
 *
 * implements: REQ-AUTH-001
 *
 * The wolfSSL headers are included ONLY here; the shim's public surface
 * (crypto_shim.h) is byte buffers, so wolfSSL never leaks into a shipped header
 * or the export table. All X25519 scalars/points are little-endian RFC 7748
 * byte order (verified against RFC 7748 §6.1 in tests/unit/test_crypto.c).
 */
#include "crypto_shim.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <wolfssl/options.h>            /* build config — must precede wolfcrypt */
#include <wolfssl/wolfcrypt/wc_port.h>  /* wolfCrypt_Init/Cleanup */
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/asn.h>          /* DecodedCert, wc_ParseCert */
#include <wolfssl/wolfcrypt/asn_public.h>   /* wc_GetDateInfo/AsCalendarTime */

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
 * and load-bearing: a prebuilt wolfSSL can be compiled with options that change
 * struct layouts WITHOUT them appearing in its installed options.h (the MSYS2
 * 5.9.2 DLL bakes in WOLFSSL_CURVE25519_BLINDING, invisible to consumers —
 * hence its curve25519_key is 128 bytes vs 112 on Debian 5.6.6), so any
 * caller-allocated wolfSSL struct is an ABI hazard. Bytes are little-endian
 * (RFC 7748); verified against RFC 7748 §6.1 keypair + Diffie-Hellman. Both the
 * keypair (u = base point) and the ECDH (u = peer public) go through this one
 * path.
 *
 * With blinding compiled in (wolfSSL 5.8.2+ default for the C implementation)
 * every call here runs the wolfCrypt RNG, whose global mutex exists only after
 * wolfCrypt_Init() — see ehem_crypto_backend_global_init() below. Callers reach
 * this via the auth flow, which guarantees ehem_global_init() has run.
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

void ehem_serial_hex(const uint8_t *serial, size_t len, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t pos = 0;
    size_t i = 0;

    if (cap == 0) {
        return;
    }
    if (serial == NULL) {
        out[0] = '\0';
        return;
    }
    if (len > 1 && serial[0] == 0x00) {
        i = 1;   /* drop the DER sign byte */
    }
    for (; i < len && pos + 2 < cap; i++) {
        out[pos++] = HEX[(serial[i] >> 4) & 0x0F];
        out[pos++] = HEX[serial[i] & 0x0F];
    }
    out[pos] = '\0';
}

/* Decode a certificate date field (tag+len+value bytes as DecodedCert stores
 * it) into "YYYY-MM-DDTHH:MM:SSZ". Leaves out="" on any failure (tolerant). */
static void date_to_iso(const byte *cert_date, int cert_date_sz,
                        char *out, size_t cap)
{
    const byte *date;
    byte format;
    int length;
    struct tm t;

    out[0] = '\0';
    if (cert_date == NULL || cert_date_sz <= 0) {
        return;
    }
    if (wc_GetDateInfo(cert_date, cert_date_sz, &date, &format, &length) != 0) {
        return;
    }
    memset(&t, 0, sizeof t);
    if (wc_GetDateAsCalendarTime(date, length, format, &t) != 0) {
        return;
    }
    (void)snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                   t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                   t.tm_hour, t.tm_min, t.tm_sec);
}

ehem_rc ehem_cert_parse_leaf(const uint8_t *der, size_t der_len,
                             ehem_cert_fields *out)
{
    DecodedCert cert;
    int rc;

    if (der == NULL || der_len == 0 || out == NULL) {
        return EHEM_ERR_ARG;
    }
    memset(out, 0, sizeof *out);

    wc_InitDecodedCert(&cert, der, (word32)der_len, NULL);
    rc = wc_ParseCert(&cert, CERT_TYPE, NO_VERIFY, NULL);
    if (rc != 0) {
        wc_FreeDecodedCert(&cert);
        return EHEM_ERR_PROTOCOL;
    }

    ehem_serial_hex(cert.serial, (size_t)cert.serialSz, out->serial_hex,
                    sizeof out->serial_hex);
    if (cert.subjectCN != NULL && cert.subjectCNLen > 0) {
        size_t n = (size_t)cert.subjectCNLen;
        if (n >= sizeof out->subject_cn) {
            n = sizeof out->subject_cn - 1;
        }
        memcpy(out->subject_cn, cert.subjectCN, n);
        out->subject_cn[n] = '\0';
    }
    date_to_iso(cert.beforeDate, cert.beforeDateLen,
                out->not_before, sizeof out->not_before);
    date_to_iso(cert.afterDate, cert.afterDateLen,
                out->not_after, sizeof out->not_after);

    wc_FreeDecodedCert(&cert);
    return EHEM_OK;
}

ehem_rc ehem_crypto_backend_global_init(void)
{
    /* Initializes wolfSSL's global mutexes (notably the RNG mutex the blinded
     * X25519 locks on every call). Without it, the first X25519 op on Windows
     * enters an uninitialized CRITICAL_SECTION and dies with
     * EXCEPTION_ACCESS_VIOLATION inside ntdll; pthread platforms mask the
     * missing call via static mutex initializers. Reference-counted upstream,
     * so init/cleanup pairs may nest. */
    return wolfCrypt_Init() == 0 ? EHEM_OK : EHEM_ERR_PROTOCOL;
}

void ehem_crypto_backend_global_cleanup(void)
{
    (void)wolfCrypt_Cleanup();
}
