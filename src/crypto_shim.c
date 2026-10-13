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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wolfssl/options.h>            /* build config — must precede wolfcrypt */
#include <wolfssl/version.h>            /* LIBWOLFSSL_VERSION_HEX (compiled against) */
#include <wolfssl/ssl.h>                /* wolfSSL_lib_version_hex() (loaded) */
#include <wolfssl/wolfcrypt/wc_port.h>  /* wolfCrypt_Init/Cleanup */
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/asn.h>          /* DecodedCert, wc_ParseCert */
#include <wolfssl/wolfcrypt/asn_public.h>   /* wc_GetDateInfo/AsCalendarTime */
#include <wolfssl/wolfcrypt/ecc.h>          /* local ECDSA verify (M4 gate) */
#include <wolfssl/wolfcrypt/ed25519.h>      /* local Ed25519 verify */
#include <wolfssl/wolfcrypt/ed448.h>        /* local Ed448 verify */
#include <wolfssl/wolfcrypt/signature.h>    /* wc_SignatureVerify */
#include <wolfssl/wolfcrypt/error-crypt.h>  /* SIG_VERIFY_E, ASN_PARSE_E */
#include <wolfssl/wolfcrypt/aes.h>          /* AES-128-CBC (scheme A, M8) */
#include <wolfssl/wolfcrypt/sha256.h>       /* wc_Sha256Hash (BIP39 checksum) */
#include <wolfssl/wolfcrypt/random.h>       /* WC_RNG (mnemonic entropy, M10) */

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

ehem_rc ehem_kdf_pbkdf2_sha512(const uint8_t *passwd, size_t passwd_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len)
{
    if (passwd == NULL || salt == NULL || out == NULL ||
        out_len == 0 || iterations == 0) {
        return EHEM_ERR_ARG;
    }
    int rc = wc_PBKDF2(out, passwd, (int)passwd_len, salt, (int)salt_len,
                       (int)iterations, (int)out_len, WC_SHA512);
    return rc == 0 ? EHEM_OK : EHEM_ERR_PROTOCOL;
}

ehem_rc ehem_sha256(const uint8_t *in, size_t in_len,
                    uint8_t out[EHEM_SHA256_SIZE])
{
    static const uint8_t empty = 0;
    if (out == NULL || (in == NULL && in_len != 0)) {
        return EHEM_ERR_ARG;
    }
    if (in == NULL) {
        in = &empty;              /* wolfCrypt rejects a NULL pointer even for 0 bytes */
    }
    return wc_Sha256Hash(in, (word32)in_len, out) == 0 ? EHEM_OK : EHEM_ERR_PROTOCOL;
}

ehem_rc ehem_random_bytes(uint8_t *out, size_t n)
{
    WC_RNG rng;
    int rc;
    if (out == NULL || n == 0) {
        return EHEM_ERR_ARG;
    }
    if (wc_InitRng(&rng) != 0) {
        return EHEM_ERR_PROTOCOL;
    }
    rc = wc_RNG_GenerateBlock(&rng, out, (word32)n);
    wc_FreeRng(&rng);
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
    /* Clamp every field to its digit width so the formatted length is provably
     * 20 + NUL = EHEM_CERT_DATE_CAP (GCC -O3 -Wformat-truncation, which the
     * Release build of REQ-BUILD-005 runs with, cannot see that itself). */
    {
        unsigned y  = (unsigned)(t.tm_year + 1900) % 10000u;
        unsigned mo = (unsigned)(t.tm_mon + 1) % 100u;
        unsigned d  = (unsigned)t.tm_mday % 100u;
        unsigned hh = (unsigned)t.tm_hour % 100u;
        unsigned mi = (unsigned)t.tm_min % 100u;
        unsigned s  = (unsigned)t.tm_sec % 100u;
        (void)snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                       y, mo, d, hh, mi, s);
    }
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

/* --------------------------------------------------------------------------
 * Local signature verification.
 *
 * implements: REQ-OPS-001 (the gate criterion's infrastructure — a signature
 *             produced via ehem_sign() verifies locally with wolfCrypt; the
 *             binding itself lives in proto_crypto.c)
 * -------------------------------------------------------------------------- */

/* Curve id + the digest the device pairs with it (its exdsa alg table). */
static int ecdsa_curve_params(ehem_ecdsa_curve curve,
                              int *curve_id, enum wc_HashType *hash)
{
    switch (curve) {
    case EHEM_ECDSA_SECP256R1: *curve_id = ECC_SECP256R1;
                               *hash = WC_HASH_TYPE_SHA256; return 0;
    case EHEM_ECDSA_SECP384R1: *curve_id = ECC_SECP384R1;
                               *hash = WC_HASH_TYPE_SHA384; return 0;
    case EHEM_ECDSA_SECP521R1: *curve_id = ECC_SECP521R1;
                               *hash = WC_HASH_TYPE_SHA512; return 0;
    case EHEM_ECDSA_SECP256K1: *curve_id = ECC_SECP256K1;
                               *hash = WC_HASH_TYPE_SHA256; return 0;
    }
    return -1;
}

ehem_rc ehem_ecdsa_verify(ehem_ecdsa_curve curve,
                          const uint8_t *pub_x963, size_t pub_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig_der, size_t sig_len,
                          int *valid_out)
{
    int curve_id;
    enum wc_HashType hash;

    if (pub_x963 == NULL || pub_len == 0 || sig_der == NULL || sig_len == 0 ||
        valid_out == NULL || (msg == NULL && msg_len != 0) ||
        ecdsa_curve_params(curve, &curve_id, &hash) != 0) {
        return EHEM_ERR_ARG;
    }
    *valid_out = 0;

    /* Library-side allocation: the DLL sizes its own ecc_key, so a prebuilt
     * wolfSSL with layout-changing options cannot overrun our stack (the
     * lesson of the M2 curve25519 investigation). */
    ecc_key *key = wc_ecc_key_new(NULL);
    if (key == NULL) {
        return EHEM_ERR_NOMEM;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    /* Imports both compressed (0x02/0x03 — the firmware's export form) and
     * uncompressed (0x04) points. Compressed needs HAVE_COMP_KEY in the
     * wolfSSL build; a build without it (rc NOT_COMPILED_IN) is reported as
     * EHEM_ERR_UNSUPPORTED so callers/tests can tell "this build can't" from
     * "bad input" (the MSYS2 CMake wolfSSL package may lack the flag). */
    int rc = wc_ecc_import_x963_ex(pub_x963, (word32)pub_len, key, curve_id);
    if (rc == NOT_COMPILED_IN) {
        result = EHEM_ERR_UNSUPPORTED;
    } else if (rc == 0) {
        rc = wc_SignatureVerify(hash, WC_SIGNATURE_TYPE_ECC,
                                msg, (word32)msg_len,
                                sig_der, (word32)sig_len,
                                key, (word32)sizeof(ecc_key));
        if (rc == 0) {
            *valid_out = 1;
            result = EHEM_OK;
        } else if (rc == SIG_VERIFY_E) {
            result = EHEM_OK;           /* well-formed, just not a match */
        } else if (rc == ASN_PARSE_E || rc == ASN_ECC_KEY_E) {
            result = EHEM_OK;           /* signature bytes not valid DER */
        }
    }
    wc_ecc_key_free(key);
    return result;
}

ehem_rc ehem_ed25519_verify(const uint8_t pub[EHEM_ED25519_PUB_SIZE],
                            const uint8_t *msg, size_t msg_len,
                            const uint8_t *sig, size_t sig_len,
                            int *valid_out)
{
    if (pub == NULL || sig == NULL || valid_out == NULL ||
        sig_len != EHEM_ED25519_SIG_SIZE || (msg == NULL && msg_len != 0)) {
        return EHEM_ERR_ARG;
    }
    *valid_out = 0;
    if (msg == NULL) {
        msg = (const uint8_t *)"";      /* RFC 8032 allows the empty message */
    }

    ed25519_key key;
    if (wc_ed25519_init(&key) != 0) {
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    if (wc_ed25519_import_public(pub, EHEM_ED25519_PUB_SIZE, &key) == 0) {
        int res = 0;
        int rc = wc_ed25519_verify_msg(sig, EHEM_ED25519_SIG_SIZE,
                                       msg, (word32)msg_len, &res, &key);
        if (rc == 0) {
            *valid_out = (res == 1);
            result = EHEM_OK;
        } else if (rc == SIG_VERIFY_E) {
            result = EHEM_OK;           /* well-formed, just not a match */
        }
    }
    wc_ed25519_free(&key);
    return result;
}

ehem_rc ehem_ed448_verify(const uint8_t pub[EHEM_ED448_PUB_SIZE],
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig, size_t sig_len,
                          int *valid_out)
{
    if (pub == NULL || sig == NULL || valid_out == NULL ||
        sig_len != EHEM_ED448_SIG_SIZE || (msg == NULL && msg_len != 0)) {
        return EHEM_ERR_ARG;
    }
    *valid_out = 0;
    if (msg == NULL) {
        msg = (const uint8_t *)"";      /* RFC 8032 allows the empty message */
    }

    ed448_key key;
    int init_rc = wc_ed448_init(&key);
    if (init_rc == NOT_COMPILED_IN) {
        return EHEM_ERR_UNSUPPORTED;    /* build without HAVE_ED448 */
    }
    if (init_rc != 0) {
        return EHEM_ERR_PROTOCOL;
    }

    ehem_rc result = EHEM_ERR_PROTOCOL;
    if (wc_ed448_import_public(pub, EHEM_ED448_PUB_SIZE, &key) == 0) {
        int res = 0;
        int rc = wc_ed448_verify_msg(sig, EHEM_ED448_SIG_SIZE,
                                     msg, (word32)msg_len, &res, &key,
                                     NULL, 0);
        if (rc == 0) {
            *valid_out = (res == 1);
            result = EHEM_OK;
        } else if (rc == SIG_VERIFY_E) {
            result = EHEM_OK;           /* well-formed, just not a match */
        } else if (rc == NOT_COMPILED_IN) {
            result = EHEM_ERR_UNSUPPORTED;
        }
    }
    wc_ed448_free(&key);
    return result;
}

/* --- AES-128-CBC (scheme A, REQ-TEST-006) --------------------------------- */

static ehem_rc aes128_cbc_run(const uint8_t key[EHEM_AES128_KEY_SIZE],
                              const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                              const uint8_t *in, size_t len, uint8_t *out,
                              int dir)
{
    if (key == NULL || iv == NULL || in == NULL || out == NULL ||
        len == 0 || (len % EHEM_AES_BLOCK_SIZE) != 0) {
        return EHEM_ERR_ARG;
    }

    Aes aes;
    if (wc_AesInit(&aes, NULL, INVALID_DEVID) != 0) {
        return EHEM_ERR_PROTOCOL;
    }
    int rc = wc_AesSetKey(&aes, key, EHEM_AES128_KEY_SIZE, iv, dir);
    if (rc == 0) {
        rc = (dir == AES_ENCRYPTION)
                 ? wc_AesCbcEncrypt(&aes, out, in, (word32)len)
                 : wc_AesCbcDecrypt(&aes, out, in, (word32)len);
    }
    wc_AesFree(&aes);
    return (rc == 0) ? EHEM_OK : EHEM_ERR_PROTOCOL;
}

ehem_rc ehem_aes128_cbc_encrypt(const uint8_t key[EHEM_AES128_KEY_SIZE],
                                const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                                const uint8_t *in, size_t len, uint8_t *out)
{
    return aes128_cbc_run(key, iv, in, len, out, AES_ENCRYPTION);
}

ehem_rc ehem_aes128_cbc_decrypt(const uint8_t key[EHEM_AES128_KEY_SIZE],
                                const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                                const uint8_t *in, size_t len, uint8_t *out)
{
    return aes128_cbc_run(key, iv, in, len, out, AES_DECRYPTION);
}

/* --------------------------------------------------------------------------
 * Backend compatibility check (REQ-API-010)
 *
 * implements: REQ-API-010 (ehem_global_init refuses a wolfSSL other than the
 *             one the SDK was built against — exact version + a guard-banded
 *             known-answer probe of every wolfCrypt struct this file allocates
 *             itself; a refusal is EHEM_ERR_BACKEND_MISMATCH and leaves the
 *             process-global state untouched)
 *
 * Why: wolfCrypt's API hands the caller the structs (Hmac, WC_RNG, DecodedCert,
 * Aes, ed25519_key, ed448_key above), and their sizes depend on the library's
 * version AND its build options. The 1.1.0 release hem-tool loaded a same-SONAME
 * wolfSSL 5.6.6 instead of its 5.7.2 companion and wc_HmacInit overran a stack
 * Hmac (896 bytes vs the 784 we had sized) — "stack smashing detected"
 * (KNOWN-ISSUES). The version check catches a different release; the probes
 * catch a same-version library built with other options, which the version
 * cannot see. ecc_key is library-allocated (wc_ecc_key_new) and needs no probe.
 *
 * Each probe places ONE struct between two canary bands in a heap box, runs the
 * struct's init / one operation with a known answer / free, then checks both
 * bands and the answer. An oversized foreign struct overruns into the trailing
 * band (which is deliberately large so the overrun lands in our bytes, not past
 * them); a layout difference gives a wrong answer. Heap, not stack: a smashed
 * stack is exactly what this check exists to prevent.
 * -------------------------------------------------------------------------- */

/* Test seam (crypto_shim.h). Never exported; unit tests reach it through the
 * static library. */
static int g_force_mismatch = EHEM_CRYPTO_FORCE_NONE;

void ehem_crypto_test_force_mismatch(int mode)
{
    g_force_mismatch = mode;
}

#define PROBE_LEAD   64u     /* leading canary band (keeps 16-byte alignment) */
#define PROBE_TRAIL  4096u   /* trailing band: > any plausible struct growth
                              * (the 5.6.6/5.7.2 DecodedCert delta was 1048) */
#define PROBE_CANARY 0xA5u

typedef struct probe_box {
    uint8_t *mem;            /* PROBE_LEAD + obj_size + PROBE_TRAIL bytes */
    size_t   obj_size;       /* OUR compile-time sizeof of the struct */
} probe_box;

/* Probe verdicts. NOMEM is kept apart from a mismatch: a failed malloc on a
 * healthy library must not read as "wrong library". */
#define PROBE_PASS      1
#define PROBE_MISMATCH  0
#define PROBE_NOMEM   (-1)

/* Open a box for a struct of `obj_size` bytes; returns the struct's address
 * (16-byte aligned) or NULL when out of memory. Every byte starts as canary. */
static void *probe_open(probe_box *b, size_t obj_size)
{
    size_t total = PROBE_LEAD + obj_size + PROBE_TRAIL;
    b->obj_size = obj_size;
    b->mem = malloc(total);
    if (b->mem == NULL) {
        return NULL;
    }
    memset(b->mem, PROBE_CANARY, total);
    return b->mem + PROBE_LEAD;
}

/* Free the box; 1 if both canary bands are intact, 0 if the struct's user
 * wrote outside the bytes we sized for it. */
static int probe_close(probe_box *b)
{
    const uint8_t *trail = b->mem + PROBE_LEAD + b->obj_size;
    int ok = 1;
    size_t i;
    for (i = 0; i < PROBE_LEAD; i++) {
        ok &= (b->mem[i] == PROBE_CANARY);
    }
    for (i = 0; i < PROBE_TRAIL; i++) {
        ok &= (trail[i] == PROBE_CANARY);
    }
    free(b->mem);
    b->mem = NULL;
    return ok;
}

/* Hmac — RFC 4231 test case 1: HMAC-SHA256(key = 20 x 0x0b, "Hi There"). */
static int probe_hmac(void)
{
    static const uint8_t key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t expect[EHEM_SHA256_SIZE] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce,
        0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    uint8_t out[EHEM_SHA256_SIZE];
    probe_box box;
    int answer_ok = 0;

    Hmac *h = probe_open(&box, sizeof(Hmac));
    if (h == NULL) {
        return PROBE_NOMEM;
    }
    if (wc_HmacInit(h, NULL, INVALID_DEVID) == 0) {
        if (wc_HmacSetKey(h, WC_SHA256, key, (word32)sizeof key) == 0 &&
            wc_HmacUpdate(h, (const byte *)"Hi There", 8) == 0 &&
            wc_HmacFinal(h, out) == 0) {
            /* Seam: a forced probe failure corrupts the answer, so the
             * comparison path (not a shortcut) produces the refusal. */
            if (g_force_mismatch == EHEM_CRYPTO_FORCE_PROBE) {
                out[0] ^= 0x01;
            }
            answer_ok = (memcmp(out, expect, sizeof out) == 0);
        }
        wc_HmacFree(h);
    }
    return probe_close(&box) && answer_ok;
}

/* WC_RNG — init / one 16-byte draw / free. There is no known answer for a
 * DRBG; the canaries are the verdict. A failed init is NOT a mismatch (OS
 * entropy is involved; it is the EHEM_ERR_PROTOCOL ehem_random_bytes reports). */
static int probe_rng(void)
{
    uint8_t draw[16];
    probe_box box;
    int rc_init, rc_draw = -1;

    WC_RNG *rng = probe_open(&box, sizeof(WC_RNG));
    if (rng == NULL) {
        return PROBE_NOMEM;
    }
    rc_init = wc_InitRng(rng);
    if (rc_init == 0) {
        rc_draw = wc_RNG_GenerateBlock(rng, draw, (word32)sizeof draw);
        wc_FreeRng(rng);
    }
    if (!probe_close(&box)) {
        return PROBE_MISMATCH;
    }
    if (rc_init != 0) {
        return PROBE_NOMEM;             /* healthy library, no entropy: not ours */
    }
    return rc_draw == 0 ? PROBE_PASS : PROBE_MISMATCH;
}

/* DecodedCert — parse a tiny self-signed EC leaf embedded here (no dependency
 * on test fixtures) and compare the fields ehem_cert_parse_leaf() reads: a
 * library whose DecodedCert lays those fields out differently reads garbage.
 *   openssl ecparam -name prime256v1 -genkey -noout -out k.pem
 *   openssl req -x509 -new -key k.pem -subj /CN=ehem-probe -days 36500 \
 *       -set_serial 0x0102030405 -sha256 -out probe.pem   (2026-10-12) */
static const uint8_t probe_cert_der[375] = {
    0x30, 0x82, 0x01, 0x73, 0x30, 0x82, 0x01, 0x18, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x30, 0x0a, 0x06, 0x08,
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30, 0x15, 0x31, 0x13,
    0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x0a, 0x65, 0x68, 0x65,
    0x6d, 0x2d, 0x70, 0x72, 0x6f, 0x62, 0x65, 0x30, 0x20, 0x17, 0x0d, 0x32,
    0x36, 0x31, 0x30, 0x31, 0x32, 0x31, 0x39, 0x35, 0x33, 0x33, 0x32, 0x5a,
    0x18, 0x0f, 0x32, 0x31, 0x32, 0x36, 0x30, 0x39, 0x31, 0x38, 0x31, 0x39,
    0x35, 0x33, 0x33, 0x32, 0x5a, 0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06,
    0x03, 0x55, 0x04, 0x03, 0x0c, 0x0a, 0x65, 0x68, 0x65, 0x6d, 0x2d, 0x70,
    0x72, 0x6f, 0x62, 0x65, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
    0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xff, 0x64, 0x75, 0x41, 0xa4,
    0x72, 0x17, 0x63, 0x66, 0xfb, 0xe6, 0x98, 0x1f, 0x01, 0x4d, 0x85, 0xe2,
    0xa9, 0x39, 0xfd, 0xe9, 0x79, 0xaf, 0x36, 0xb6, 0xe1, 0xfb, 0x4f, 0x7f,
    0x0f, 0x44, 0x6c, 0x22, 0xf2, 0x72, 0xbd, 0xb8, 0x60, 0x7d, 0x57, 0x35,
    0x1e, 0x6e, 0x42, 0x78, 0xe3, 0xbc, 0x86, 0x1a, 0xda, 0xad, 0x9a, 0xa1,
    0x58, 0xc5, 0x21, 0x3f, 0xec, 0x9d, 0xcf, 0xc9, 0x43, 0xe6, 0x85, 0xa3,
    0x53, 0x30, 0x51, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16,
    0x04, 0x14, 0xa8, 0xd5, 0x28, 0x21, 0x14, 0xa4, 0xdb, 0x6b, 0x9d, 0x70,
    0x1d, 0x0b, 0x44, 0x9e, 0xfd, 0x99, 0x74, 0x72, 0x02, 0x3a, 0x30, 0x1f,
    0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xa8,
    0xd5, 0x28, 0x21, 0x14, 0xa4, 0xdb, 0x6b, 0x9d, 0x70, 0x1d, 0x0b, 0x44,
    0x9e, 0xfd, 0x99, 0x74, 0x72, 0x02, 0x3a, 0x30, 0x0f, 0x06, 0x03, 0x55,
    0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff,
    0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02,
    0x03, 0x49, 0x00, 0x30, 0x46, 0x02, 0x21, 0x00, 0xb4, 0x56, 0xac, 0xd4,
    0x16, 0x59, 0xa1, 0xa1, 0x61, 0xd7, 0x93, 0x9e, 0x5a, 0x19, 0x1b, 0x79,
    0x0d, 0x16, 0xf8, 0xcd, 0xbe, 0xfd, 0x1c, 0x2f, 0x32, 0x2b, 0x92, 0x0d,
    0xd2, 0xa0, 0x39, 0xa5, 0x02, 0x21, 0x00, 0xd5, 0xd1, 0x06, 0x1d, 0x63,
    0x3c, 0xfa, 0x5b, 0xca, 0xa5, 0xed, 0x73, 0x40, 0xa5, 0x3f, 0xb3, 0x58,
    0x33, 0x8e, 0x8a, 0x6b, 0xf5, 0x2f, 0xdf, 0xb0, 0xbe, 0x78, 0x2f, 0xee,
    0x1a, 0x77, 0xa5
};

static int probe_decoded_cert(void)
{
    char serial[EHEM_CERT_SERIAL_HEX_CAP];
    char cn[EHEM_CERT_CN_CAP];
    char nb[EHEM_CERT_DATE_CAP], na[EHEM_CERT_DATE_CAP];
    probe_box box;
    int answer_ok = 0;

    DecodedCert *c = probe_open(&box, sizeof(DecodedCert));
    if (c == NULL) {
        return PROBE_NOMEM;
    }
    wc_InitDecodedCert(c, probe_cert_der, (word32)sizeof probe_cert_der, NULL);
    if (wc_ParseCert(c, CERT_TYPE, NO_VERIFY, NULL) == 0) {
        size_t n = (c->subjectCN != NULL && c->subjectCNLen > 0)
                       ? (size_t)c->subjectCNLen : 0;
        if (n >= sizeof cn) {
            n = sizeof cn - 1;
        }
        memcpy(cn, c->subjectCN, n);
        cn[n] = '\0';
        ehem_serial_hex(c->serial, (size_t)c->serialSz, serial, sizeof serial);
        date_to_iso(c->beforeDate, c->beforeDateLen, nb, sizeof nb);
        date_to_iso(c->afterDate, c->afterDateLen, na, sizeof na);
        answer_ok = strcmp(serial, "0102030405") == 0 &&
                    strcmp(cn, "ehem-probe") == 0 &&
                    strcmp(nb, "2026-10-12T19:53:32Z") == 0 &&
                    strcmp(na, "2126-09-18T19:53:32Z") == 0;
    }
    wc_FreeDecodedCert(c);
    return probe_close(&box) && answer_ok;
}

/* Aes — FIPS-197 C.1 (AES-128, one block; CBC with a zero IV == ECB). */
static int probe_aes(void)
{
    static const uint8_t key[EHEM_AES128_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t iv[EHEM_AES_BLOCK_SIZE] = {0};
    static const uint8_t plain[EHEM_AES_BLOCK_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t expect[EHEM_AES_BLOCK_SIZE] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
    };
    uint8_t out[EHEM_AES_BLOCK_SIZE];
    probe_box box;
    int answer_ok = 0;

    Aes *aes = probe_open(&box, sizeof(Aes));
    if (aes == NULL) {
        return PROBE_NOMEM;
    }
    if (wc_AesInit(aes, NULL, INVALID_DEVID) == 0) {
        if (wc_AesSetKey(aes, key, EHEM_AES128_KEY_SIZE, iv, AES_ENCRYPTION) == 0 &&
            wc_AesCbcEncrypt(aes, out, plain, EHEM_AES_BLOCK_SIZE) == 0) {
            answer_ok = (memcmp(out, expect, sizeof out) == 0);
        }
        wc_AesFree(aes);
    }
    return probe_close(&box) && answer_ok;
}

/* The message both Edwards probes verify a signature over; keys and signatures
 * were produced with OpenSSL 3.0.13 (openssl genpkey / pkeyutl -sign -rawin)
 * on 2026-10-12 — a second implementation, so the answer is independent. */
static const uint8_t probe_ed_msg[31] = "encedo-hem crypto backend probe";

static const uint8_t probe_ed25519_pub[EHEM_ED25519_PUB_SIZE] = {
    0x8b, 0xf9, 0x48, 0x60, 0x9a, 0xf5, 0x6d, 0xd7, 0xdf, 0x9c, 0xae, 0xe0,
    0x13, 0x8b, 0x5a, 0xbf, 0xd1, 0x96, 0x07, 0xb3, 0xf1, 0x2b, 0xd1, 0x14,
    0x59, 0x62, 0x39, 0xe4, 0x68, 0xb0, 0x1f, 0x93
};
static const uint8_t probe_ed25519_sig[EHEM_ED25519_SIG_SIZE] = {
    0x2e, 0xdc, 0x19, 0x47, 0x7e, 0xb0, 0xa3, 0x6d, 0xdc, 0xf3, 0x1f, 0xae,
    0x0a, 0x32, 0xfa, 0xd1, 0x4c, 0xb4, 0x28, 0xc5, 0xa4, 0x4c, 0x12, 0x7c,
    0x39, 0xaa, 0xf7, 0x96, 0x60, 0x91, 0xf6, 0xd2, 0xb0, 0xa9, 0xb4, 0x08,
    0x20, 0x48, 0x04, 0x3a, 0x0d, 0x25, 0x72, 0xb8, 0x2c, 0x34, 0x4e, 0xac,
    0x9b, 0x2d, 0x1b, 0xce, 0x66, 0x52, 0xbf, 0xae, 0x47, 0xeb, 0x6d, 0xb3,
    0xb8, 0x67, 0xa9, 0x03
};
static const uint8_t probe_ed448_pub[EHEM_ED448_PUB_SIZE] = {
    0x15, 0xf7, 0xf1, 0x11, 0xfc, 0xac, 0xae, 0x64, 0x2a, 0x0b, 0x36, 0x39,
    0x9c, 0xd3, 0x9b, 0x88, 0x7f, 0x13, 0x42, 0xfd, 0x17, 0x34, 0x20, 0x95,
    0x5b, 0x65, 0x40, 0xee, 0x01, 0x7e, 0x1d, 0x05, 0x23, 0x97, 0x11, 0xcf,
    0x3a, 0xfb, 0x48, 0x69, 0x47, 0x80, 0x05, 0x91, 0x52, 0x39, 0x8f, 0x39,
    0x34, 0xf1, 0x8b, 0x2b, 0x99, 0x9c, 0x58, 0x21, 0x80
};
static const uint8_t probe_ed448_sig[EHEM_ED448_SIG_SIZE] = {
    0x91, 0x53, 0x1f, 0x50, 0xbd, 0x1c, 0xfd, 0xca, 0x70, 0xaa, 0x2d, 0x3b,
    0x5d, 0x4d, 0x60, 0xcb, 0x31, 0xd8, 0x60, 0x33, 0x57, 0xae, 0xec, 0xea,
    0x1b, 0x58, 0x43, 0xf6, 0xd9, 0xba, 0x5d, 0x11, 0x2e, 0xd6, 0xde, 0x46,
    0xb3, 0x15, 0x15, 0x86, 0x91, 0x36, 0x6e, 0x24, 0x95, 0xee, 0x17, 0x5f,
    0x06, 0x2a, 0x75, 0x91, 0x5c, 0xb3, 0x00, 0xba, 0x80, 0xaf, 0xb4, 0x28,
    0xa1, 0xd9, 0xcf, 0x95, 0x0b, 0xfa, 0x42, 0x6c, 0x2c, 0x5a, 0x7c, 0xee,
    0xe4, 0xad, 0x98, 0xa5, 0xd4, 0xf0, 0xc1, 0xe6, 0x6f, 0x4f, 0x39, 0xbc,
    0x5f, 0x8d, 0xe5, 0xd8, 0xad, 0xbb, 0x2e, 0xec, 0x29, 0x20, 0xd2, 0xf6,
    0x15, 0x56, 0x67, 0x7e, 0x74, 0xf2, 0x17, 0x74, 0x6c, 0xe0, 0xd5, 0x19,
    0xb8, 0x84, 0x96, 0x93, 0x2a, 0x00
};

/* ed25519_key — import the public key, verify the signature. */
static int probe_ed25519(void)
{
    probe_box box;
    int answer_ok = 0;

    ed25519_key *k = probe_open(&box, sizeof(ed25519_key));
    if (k == NULL) {
        return PROBE_NOMEM;
    }
    if (wc_ed25519_init(k) == 0) {
        int res = 0;
        if (wc_ed25519_import_public(probe_ed25519_pub, EHEM_ED25519_PUB_SIZE,
                                     k) == 0 &&
            wc_ed25519_verify_msg(probe_ed25519_sig, EHEM_ED25519_SIG_SIZE,
                                  probe_ed_msg, (word32)sizeof probe_ed_msg,
                                  &res, k) == 0) {
            answer_ok = (res == 1);
        }
        wc_ed25519_free(k);
    }
    return probe_close(&box) && answer_ok;
}

/* ed448_key — same shape. A build without HAVE_ED448 (NOT_COMPILED_IN) has no
 * struct to probe; ehem_ed448_verify reports UNSUPPORTED on it, so pass. */
static int probe_ed448(void)
{
    probe_box box;
    int answer_ok = 0;
    int rc_init;

    ed448_key *k = probe_open(&box, sizeof(ed448_key));
    if (k == NULL) {
        return PROBE_NOMEM;
    }
    rc_init = wc_ed448_init(k);
    if (rc_init == NOT_COMPILED_IN) {
        answer_ok = 1;
    } else if (rc_init == 0) {
        int res = 0;
        if (wc_ed448_import_public(probe_ed448_pub, EHEM_ED448_PUB_SIZE,
                                   k) == 0 &&
            wc_ed448_verify_msg(probe_ed448_sig, EHEM_ED448_SIG_SIZE,
                                probe_ed_msg, (word32)sizeof probe_ed_msg,
                                &res, k, NULL, 0) == 0) {
            answer_ok = (res == 1);
        }
        wc_ed448_free(k);
    }
    return probe_close(&box) && answer_ok;
}

/* Run the six probes in the order the SDK first touches the structs. Stops at
 * the first verdict that is not PASS: a mismatch is final, and NOMEM must not
 * be followed by more allocation. */
static ehem_rc run_struct_probes(void)
{
    static int (*const probes[])(void) = {
        probe_hmac, probe_rng, probe_decoded_cert,
        probe_aes, probe_ed25519, probe_ed448
    };
    size_t i;
    for (i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        int v = probes[i]();
        if (v == PROBE_MISMATCH) {
            return EHEM_ERR_BACKEND_MISMATCH;
        }
        if (v == PROBE_NOMEM) {
            return EHEM_ERR_NOMEM;
        }
    }
    return EHEM_OK;
}

ehem_rc ehem_crypto_backend_global_init(void)
{
    ehem_rc rc;

    /* 1. Exact version: wolfSSL_lib_version_hex() is a plain accessor — no
     *    struct crosses the boundary, so it is safe to ask ANY library. Runs
     *    before wolfCrypt_Init so a refused library has had nothing of ours
     *    touch its global state. */
    {
        word32 loaded = wolfSSL_lib_version_hex();
        if (g_force_mismatch == EHEM_CRYPTO_FORCE_VERSION) {
            loaded ^= 0x1u;
        }
        if (loaded != (word32)LIBWOLFSSL_VERSION_HEX) {
            return EHEM_ERR_BACKEND_MISMATCH;
        }
    }

    /* 2. wolfCrypt_Init: wolfSSL's global mutexes (notably the RNG mutex the
     *    blinded X25519 — and the WC_RNG probe — lock). Without it the first
     *    RNG use on Windows enters an uninitialized CRITICAL_SECTION and dies
     *    with EXCEPTION_ACCESS_VIOLATION inside ntdll; pthread platforms mask
     *    the missing call via static mutex initializers. Reference-counted
     *    upstream, so init/cleanup pairs may nest. */
    if (wolfCrypt_Init() != 0) {
        return EHEM_ERR_PROTOCOL;
    }

    /* 3. Struct probes. A refusal undoes step 2 so the process-global state is
     *    as it was: ehem_global_cleanup() stays a no-op and a later init
     *    re-checks (REQ-API-002 idempotency holds across a refusal). */
    rc = run_struct_probes();
    if (rc != EHEM_OK) {
        (void)wolfCrypt_Cleanup();
        return rc;
    }
    return EHEM_OK;
}

void ehem_crypto_backend_global_cleanup(void)
{
    (void)wolfCrypt_Cleanup();
}

/* implements: REQ-API-011 — compiled = the headers this file was built with,
 * loaded = the library's own report (a plain string accessor, like the
 * version-hex check above, so it is safe to ask a foreign library). */
void ehem_crypto_backend_describe(ehem_build_info *out)
{
    const char *loaded = wolfSSL_lib_version();

    out->crypto_backend          = "wolfSSL";
    out->crypto_version_compiled = LIBWOLFSSL_VERSION_STRING;
    out->crypto_version_loaded   = (loaded != NULL) ? loaded : "unknown";
#ifdef EHEM_CRYPTO_LINK_STATIC
    out->crypto_link             = "static";
#else
    out->crypto_link             = "dynamic";
#endif
}
