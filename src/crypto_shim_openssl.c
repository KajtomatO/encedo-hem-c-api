/*
 * crypto_shim_openssl.c — the OpenSSL 3 implementation of the crypto shim:
 * EHEM_CRYPTO_BACKEND=openssl. crypto_shim.h is the contract, unchanged — the
 * same byte buffers, return codes and tri-state verify results as
 * crypto_shim_wolfssl.c; crypto_shim_common.c holds what both share.
 *
 * implements: REQ-BUILD-007 (the OpenSSL 3 backend: EVP interfaces of the
 *             default provider only), REQ-AUTH-001, REQ-OPS-001
 *
 * The OpenSSL headers are included ONLY here. Every OpenSSL object the shim
 * uses (EVP_PKEY, EVP_MD_CTX, EVP_CIPHER_CTX, X509) is opaque and allocated
 * by the library, so the caller-sized-struct hazard REQ-API-010 probes for on
 * wolfSSL cannot occur. A failing OpenSSL call leaves an entry on the thread's
 * error queue; the shim clears it before returning, so libcurl (OpenSSL TLS in
 * the -static builds) never reads a stale error of ours.
 */
#include "crypto_shim.h"
#include "crypto_shim_common.h"

#include <limits.h>
#include <string.h>
#include <time.h>

#include <openssl/asn1.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>          /* ECDSA_SIG (DER pre-check) */
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

/* A failure of the library: clear its error queue, report PROTOCOL. */
static ehem_rc engine_failure(void)
{
    ERR_clear_error();
    return EHEM_ERR_PROTOCOL;
}

static ehem_rc pbkdf2(const EVP_MD *md, const uint8_t *passwd, size_t passwd_len,
                      const uint8_t *salt, size_t salt_len, uint32_t iterations,
                      uint8_t *out, size_t out_len)
{
    if (passwd == NULL || salt == NULL || out == NULL ||
        out_len == 0 || iterations == 0) {
        return EHEM_ERR_ARG;
    }
    if (passwd_len > INT_MAX || salt_len > INT_MAX || out_len > INT_MAX ||
        iterations > INT_MAX) {
        return EHEM_ERR_PROTOCOL;     /* beyond the engine's int interface */
    }
    if (PKCS5_PBKDF2_HMAC((const char *)passwd, (int)passwd_len, salt,
                          (int)salt_len, (int)iterations, md, (int)out_len,
                          out) != 1) {
        return engine_failure();
    }
    return EHEM_OK;
}

ehem_rc ehem_kdf_pbkdf2_sha256(const uint8_t *passwd, size_t passwd_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len)
{
    return pbkdf2(EVP_sha256(), passwd, passwd_len, salt, salt_len, iterations,
                  out, out_len);
}

ehem_rc ehem_kdf_pbkdf2_sha512(const uint8_t *passwd, size_t passwd_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len)
{
    return pbkdf2(EVP_sha512(), passwd, passwd_len, salt, salt_len, iterations,
                  out, out_len);
}

ehem_rc ehem_sha256(const uint8_t *in, size_t in_len,
                    uint8_t out[EHEM_SHA256_SIZE])
{
    static const uint8_t empty = 0;
    unsigned int n = 0;

    if (out == NULL || (in == NULL && in_len != 0)) {
        return EHEM_ERR_ARG;
    }
    if (in == NULL) {
        in = &empty;
    }
    if (EVP_Digest(in, in_len, out, &n, EVP_sha256(), NULL) != 1 ||
        n != EHEM_SHA256_SIZE) {
        return engine_failure();
    }
    return EHEM_OK;
}

ehem_rc ehem_random_bytes(uint8_t *out, size_t n)
{
    if (out == NULL || n == 0) {
        return EHEM_ERR_ARG;
    }
    while (n > 0) {                   /* RAND_bytes takes an int count */
        int chunk = (n > (size_t)INT_MAX) ? INT_MAX : (int)n;
        if (RAND_bytes(out, chunk) != 1) {
            return engine_failure();
        }
        out += chunk;
        n -= (size_t)chunk;
    }
    return EHEM_OK;
}

ehem_rc ehem_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[EHEM_SHA256_SIZE])
{
    static const uint8_t empty = 0;
    size_t n = 0;

    if (key == NULL || out == NULL || (msg == NULL && msg_len != 0)) {
        return EHEM_ERR_ARG;
    }
    if (msg == NULL) {
        msg = &empty;
    }
    if (EVP_Q_mac(NULL, "HMAC", NULL, "SHA256", NULL, key, key_len, msg,
                  msg_len, out, EHEM_SHA256_SIZE, &n) == NULL ||
        n != EHEM_SHA256_SIZE) {
        return engine_failure();
    }
    return EHEM_OK;
}

/* --- X25519 (RFC 7748; all values little-endian) ---------------------------- */

ehem_rc ehem_x25519_keypair_from_seed(const uint8_t seed[EHEM_X25519_KEYSIZE],
                                      uint8_t priv_out[EHEM_X25519_KEYSIZE],
                                      uint8_t pub_out[EHEM_X25519_KEYSIZE])
{
    uint8_t priv[EHEM_X25519_KEYSIZE];
    ehem_rc result = EHEM_OK;

    if (seed == NULL) {
        return EHEM_ERR_ARG;
    }
    /* RFC 7748 clamp of the little-endian scalar — the exported private key is
     * the clamped one, as on the wolfSSL backend (X25519 itself clamps too). */
    memcpy(priv, seed, EHEM_X25519_KEYSIZE);
    priv[0]  &= 248;
    priv[31] &= 127;
    priv[31] |= 64;

    if (pub_out != NULL) {
        size_t len = EHEM_X25519_KEYSIZE;
        EVP_PKEY *pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv,
                                                    sizeof priv);
        if (pk == NULL || EVP_PKEY_get_raw_public_key(pk, pub_out, &len) != 1 ||
            len != EHEM_X25519_KEYSIZE) {
            result = engine_failure();
        }
        EVP_PKEY_free(pk);            /* OpenSSL clears its private copy */
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
    EVP_PKEY *mine;
    EVP_PKEY *peer;
    EVP_PKEY_CTX *ctx = NULL;
    size_t len = EHEM_X25519_KEYSIZE;
    ehem_rc result = EHEM_ERR_PROTOCOL;

    if (priv == NULL || peer_pub == NULL || out == NULL) {
        return EHEM_ERR_ARG;
    }
    mine = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv,
                                        EHEM_X25519_KEYSIZE);
    peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_pub,
                                       EHEM_X25519_KEYSIZE);
    if (mine != NULL && peer != NULL) {
        ctx = EVP_PKEY_CTX_new(mine, NULL);
    }
    /* The derivation refuses a small-order peer (an all-zero shared secret). */
    if (ctx != NULL && EVP_PKEY_derive_init(ctx) == 1 &&
        EVP_PKEY_derive_set_peer(ctx, peer) == 1 &&
        EVP_PKEY_derive(ctx, out, &len) == 1 && len == EHEM_X25519_KEYSIZE) {
        result = EHEM_OK;
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(mine);
    return (result == EHEM_OK) ? EHEM_OK : engine_failure();
}

/* --- X.509 leaf inspection (REQ-SYS-006) ------------------------------------ */

static void asn1_time_to_iso(const ASN1_TIME *t, char *out, size_t cap)
{
    struct tm tm;

    out[0] = '\0';
    memset(&tm, 0, sizeof tm);
    if (t == NULL || ASN1_TIME_to_tm(t, &tm) != 1) {
        ERR_clear_error();
        return;
    }
    ehem_crypto_iso_date(&tm, out, cap);
}

ehem_rc ehem_cert_parse_leaf(const uint8_t *der, size_t der_len,
                             ehem_cert_fields *out)
{
    const unsigned char *p = der;
    const ASN1_INTEGER *serial;
    X509 *cert;

    if (der == NULL || der_len == 0 || out == NULL) {
        return EHEM_ERR_ARG;
    }
    memset(out, 0, sizeof *out);
    if (der_len > (size_t)LONG_MAX) {
        return EHEM_ERR_PROTOCOL;
    }
    /* d2i_X509 reads the FIRST certificate of a concatenated DER chain. */
    cert = d2i_X509(NULL, &p, (long)der_len);
    if (cert == NULL) {
        return engine_failure();
    }

    /* OpenSSL keeps an INTEGER's magnitude without the DER sign byte; the
     * formatter drops that byte on the other path, so both agree. */
    serial = X509_get0_serialNumber(cert);
    if (serial != NULL) {
        ehem_serial_hex(ASN1_STRING_get0_data(serial),
                        (size_t)ASN1_STRING_length(serial),
                        out->serial_hex, sizeof out->serial_hex);
    }
    /* Copies at most cap - 1 bytes and terminates; -1 = no CN. */
    if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                                  out->subject_cn,
                                  (int)sizeof out->subject_cn) < 0) {
        out->subject_cn[0] = '\0';
    }
    asn1_time_to_iso(X509_get0_notBefore(cert), out->not_before,
                     sizeof out->not_before);
    asn1_time_to_iso(X509_get0_notAfter(cert), out->not_after,
                     sizeof out->not_after);

    X509_free(cert);
    ERR_clear_error();
    return EHEM_OK;
}

/* --------------------------------------------------------------------------
 * Local signature verification.
 *
 * implements: REQ-OPS-001 (a signature produced via ehem_sign() verifies
 *             locally — here with OpenSSL; the binding is proto_crypto.c)
 * -------------------------------------------------------------------------- */

/* One-shot EVP_DigestVerify: 1 = valid, 0 = a clean mismatch, anything else
 * an engine failure. `md` NULL for EdDSA (pure, no pre-hash). */
static ehem_rc digest_verify(EVP_PKEY *key, const EVP_MD *md,
                             const uint8_t *sig, size_t sig_len,
                             const uint8_t *msg, size_t msg_len,
                             int *valid_out)
{
    static const uint8_t empty = 0;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    ehem_rc result = EHEM_ERR_PROTOCOL;

    if (msg == NULL) {
        msg = &empty;                 /* RFC 8032 allows the empty message */
    }
    if (mctx != NULL && EVP_DigestVerifyInit(mctx, NULL, md, NULL, key) == 1) {
        int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len);
        if (rc == 1) {
            *valid_out = 1;
            result = EHEM_OK;
        } else if (rc == 0) {
            result = EHEM_OK;         /* well-formed, just not a match */
        }
    }
    EVP_MD_CTX_free(mctx);
    ERR_clear_error();
    return result;
}

/* Curve name + the digest the device pairs with it (its exdsa alg table). */
static const char *ecdsa_group(ehem_ecdsa_curve curve, const EVP_MD **md)
{
    switch (curve) {
    case EHEM_ECDSA_SECP256R1: *md = EVP_sha256(); return SN_X9_62_prime256v1;
    case EHEM_ECDSA_SECP384R1: *md = EVP_sha384(); return SN_secp384r1;
    case EHEM_ECDSA_SECP521R1: *md = EVP_sha512(); return SN_secp521r1;
    case EHEM_ECDSA_SECP256K1: *md = EVP_sha256(); return SN_secp256k1;
    }
    return NULL;
}

/* An EC public key from an X9.63 point, compressed (0x02/0x03 — the firmware's
 * export form) or uncompressed (0x04). The import decodes the point and checks
 * it lies on the curve: NULL for anything else. */
static EVP_PKEY *ec_public_key(const char *group, const uint8_t *point,
                               size_t len)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    EVP_PKEY *key = NULL;
    OSSL_PARAM params[3];

    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                 (char *)group, 0);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                                  (void *)point, len);
    params[2] = OSSL_PARAM_construct_end();
    if (ctx == NULL || EVP_PKEY_fromdata_init(ctx) != 1 ||
        EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params) != 1) {
        key = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

/* 1 if `sig` is exactly one canonically DER-encoded ECDSA-Sig-Value. OpenSSL's
 * verify reports a malformed signature as an error, which this shim must
 * report as a clean mismatch (crypto_shim.h; the wolfSSL backend's behaviour). */
static int is_der_ecdsa_sig(const uint8_t *sig, size_t len)
{
    const unsigned char *p = sig;
    unsigned char *der = NULL;
    ECDSA_SIG *s;
    int n;
    int ok = 0;

    if (len > (size_t)LONG_MAX) {
        return 0;
    }
    s = d2i_ECDSA_SIG(NULL, &p, (long)len);
    if (s != NULL) {
        n = i2d_ECDSA_SIG(s, &der);
        ok = n > 0 && (size_t)n == len && memcmp(der, sig, len) == 0;
        OPENSSL_free(der);
        ECDSA_SIG_free(s);
    }
    ERR_clear_error();
    return ok;
}

ehem_rc ehem_ecdsa_verify(ehem_ecdsa_curve curve,
                          const uint8_t *pub_x963, size_t pub_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig_der, size_t sig_len,
                          int *valid_out)
{
    const EVP_MD *md = NULL;
    const char *group;
    EVP_PKEY *key;
    ehem_rc result;

    if (pub_x963 == NULL || pub_len == 0 || sig_der == NULL || sig_len == 0 ||
        valid_out == NULL || (msg == NULL && msg_len != 0)) {
        return EHEM_ERR_ARG;
    }
    group = ecdsa_group(curve, &md);
    if (group == NULL) {
        return EHEM_ERR_ARG;
    }
    *valid_out = 0;

    key = ec_public_key(group, pub_x963, pub_len);
    if (key == NULL) {
        return engine_failure();      /* not a point of this curve */
    }
    if (!is_der_ecdsa_sig(sig_der, sig_len)) {
        result = EHEM_OK;             /* signature bytes not valid DER */
    } else {
        result = digest_verify(key, md, sig_der, sig_len, msg, msg_len,
                               valid_out);
    }
    EVP_PKEY_free(key);
    return result;
}

static ehem_rc eddsa_verify(int type, const uint8_t *pub, size_t pub_len,
                            const uint8_t *msg, size_t msg_len,
                            const uint8_t *sig, size_t sig_len, int *valid_out)
{
    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(type, NULL, pub, pub_len);
    ehem_rc result;

    if (key == NULL) {
        return engine_failure();
    }
    result = digest_verify(key, NULL, sig, sig_len, msg, msg_len, valid_out);
    EVP_PKEY_free(key);
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
    return eddsa_verify(EVP_PKEY_ED25519, pub, EHEM_ED25519_PUB_SIZE, msg,
                        msg_len, sig, sig_len, valid_out);
}

/* OpenSSL 3 always has Ed448 — EHEM_ERR_UNSUPPORTED never comes from here. */
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
    return eddsa_verify(EVP_PKEY_ED448, pub, EHEM_ED448_PUB_SIZE, msg, msg_len,
                        sig, sig_len, valid_out);
}

/* --- AES-128-CBC (scheme A, REQ-TEST-006) --------------------------------- */

static ehem_rc aes128_cbc_run(const uint8_t key[EHEM_AES128_KEY_SIZE],
                              const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                              const uint8_t *in, size_t len, uint8_t *out,
                              int encrypt)
{
    EVP_CIPHER_CTX *ctx;
    int n = 0;
    int fin = 0;
    ehem_rc result = EHEM_ERR_PROTOCOL;

    if (key == NULL || iv == NULL || in == NULL || out == NULL ||
        len == 0 || (len % EHEM_AES_BLOCK_SIZE) != 0 || len > (size_t)INT_MAX) {
        return EHEM_ERR_ARG;
    }
    /* Raw blocks: no padding — the caller pads (scheme A's own rule). An
     * in-place call (in == out) is supported by EVP_CipherUpdate. */
    ctx = EVP_CIPHER_CTX_new();
    if (ctx != NULL &&
        EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv, encrypt) == 1 &&
        EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
        EVP_CipherUpdate(ctx, out, &n, in, (int)len) == 1 &&
        EVP_CipherFinal_ex(ctx, out + n, &fin) == 1 &&
        (size_t)n + (size_t)fin == len) {
        result = EHEM_OK;
    }
    EVP_CIPHER_CTX_free(ctx);
    return (result == EHEM_OK) ? EHEM_OK : engine_failure();
}

ehem_rc ehem_aes128_cbc_encrypt(const uint8_t key[EHEM_AES128_KEY_SIZE],
                                const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                                const uint8_t *in, size_t len, uint8_t *out)
{
    return aes128_cbc_run(key, iv, in, len, out, 1);
}

ehem_rc ehem_aes128_cbc_decrypt(const uint8_t key[EHEM_AES128_KEY_SIZE],
                                const uint8_t iv[EHEM_AES_BLOCK_SIZE],
                                const uint8_t *in, size_t len, uint8_t *out)
{
    return aes128_cbc_run(key, iv, in, len, out, 0);
}

/* --------------------------------------------------------------------------
 * Backend compatibility check (REQ-API-010) on OpenSSL 3
 *
 * implements: REQ-API-010 (on this backend: the loaded library's MAJOR version
 *             equals the headers' — OpenSSL keeps its ABI within a major
 *             version, so a newer 3.y than the headers is compatible — and
 *             known answers through the shim pass; a refusal is
 *             EHEM_ERR_BACKEND_MISMATCH and touches no global state)
 * -------------------------------------------------------------------------- */

/* HMAC-SHA256 (RFC 4231 test case 1) and SHA-256("abc") (FIPS 180-2 B.1). */
static ehem_rc known_answers(void)
{
    static const uint8_t key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t hmac_expect[EHEM_SHA256_SIZE] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce,
        0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    static const uint8_t sha_expect[EHEM_SHA256_SIZE] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
        0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    uint8_t out[EHEM_SHA256_SIZE];

    if (ehem_hmac_sha256(key, sizeof key, (const uint8_t *)"Hi There", 8,
                         out) != EHEM_OK) {
        return EHEM_ERR_BACKEND_MISMATCH;
    }
    /* Seam: a forced probe failure corrupts the answer, so the comparison
     * path (not a shortcut) produces the refusal. */
    if (ehem_crypto_forced_mismatch() == EHEM_CRYPTO_FORCE_PROBE) {
        out[0] ^= 0x01;
    }
    if (memcmp(out, hmac_expect, sizeof out) != 0) {
        return EHEM_ERR_BACKEND_MISMATCH;
    }
    if (ehem_sha256((const uint8_t *)"abc", 3, out) != EHEM_OK ||
        memcmp(out, sha_expect, sizeof out) != 0) {
        return EHEM_ERR_BACKEND_MISMATCH;
    }
    return EHEM_OK;
}

ehem_rc ehem_crypto_backend_global_init(void)
{
    /* 1. Same major version: a plain accessor, safe to ask any libcrypto. */
    unsigned int loaded = OPENSSL_version_major();

    if (ehem_crypto_forced_mismatch() == EHEM_CRYPTO_FORCE_VERSION) {
        loaded ^= 0x1u;
    }
    if (loaded != (unsigned int)OPENSSL_VERSION_MAJOR) {
        return EHEM_ERR_BACKEND_MISMATCH;
    }
    /* 2. OpenSSL 3 initialises itself on first use; the explicit call makes
     *    the moment deterministic. Repeat calls are harmless. */
    if (OPENSSL_init_crypto(0, NULL) != 1) {
        return engine_failure();
    }
    /* 3. Known answers through the shim's own primitives. */
    return known_answers();
}

/* No-op: OPENSSL_cleanup() is one-shot — after it OpenSSL cannot be used again
 * in the process, which would break REQ-API-002's init-after-cleanup. The
 * library frees its global state at process exit by itself. */
void ehem_crypto_backend_global_cleanup(void)
{
}

/* implements: REQ-API-011 — compiled = the headers this file was built with,
 * loaded = the library's own report (a plain string accessor, safe before
 * initialisation and after a refused one). */
void ehem_crypto_backend_describe(ehem_build_info *out)
{
    const char *loaded = OpenSSL_version(OPENSSL_VERSION_STRING);

    out->crypto_backend          = "OpenSSL";
    out->crypto_version_compiled = OPENSSL_VERSION_STR;
    out->crypto_version_loaded   = (loaded != NULL) ? loaded : "unknown";
#ifdef EHEM_CRYPTO_LINK_STATIC
    out->crypto_link             = "static";
#else
    out->crypto_link             = "dynamic";
#endif
}
