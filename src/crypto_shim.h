/*
 * crypto_shim.h — minimal internal crypto API for the auth flow.
 *
 * implements: REQ-AUTH-001 (the PBKDF2 / HMAC-SHA256 / X25519 primitives the
 *             passphrase login derivation is built from — ARCHITECTURE.md §5)
 *
 * INTERNAL header — not shipped in include/ehem/, never part of the public ABI.
 * It wraps wolfCrypt (wolfSSL) behind byte-buffer functions so that no wolfSSL
 * type or header ever appears in a public header or the export table (same
 * containment rule as libcurl and cJSON; enforced by the public-header check).
 *
 * All multi-byte X25519 values are little-endian (RFC 7748 byte order).
 */
#ifndef EHEM_CRYPTO_SHIM_H
#define EHEM_CRYPTO_SHIM_H

#include <stddef.h>
#include <stdint.h>

#include <ehem/ehem.h>   /* ehem_rc */

#define EHEM_X25519_KEYSIZE 32   /* X25519 scalar / point size */
#define EHEM_SHA256_SIZE    32   /* SHA-256 / HMAC-SHA256 output size */

/*
 * PBKDF2-HMAC-SHA256. Derives `out_len` bytes into `out` from the passphrase
 * and salt over `iterations` rounds. REQ-AUTH-001 pins the login to 600 000
 * iterations and a 32-byte output; those values are supplied by the caller
 * (the auth layer) so this primitive stays vector-testable.
 *
 * Returns EHEM_ERR_ARG on a NULL buffer, zero length, or zero iterations.
 */
ehem_rc ehem_kdf_pbkdf2_sha256(const uint8_t *passwd, size_t passwd_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len);

/*
 * HMAC-SHA256 over `msg` keyed with `key`; writes the 32-byte tag to `out`.
 * `msg` may be NULL only when msg_len is 0. Returns EHEM_ERR_ARG on a NULL
 * key/out (or NULL msg with nonzero length).
 */
ehem_rc ehem_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[EHEM_SHA256_SIZE]);

/*
 * Derive an X25519 keypair from a 32-byte seed (the KDF output). Writes the
 * clamped private scalar to `priv_out` (RFC 7748 clamping applied) and the
 * public key to `pub_out`; either output pointer may be NULL if not wanted.
 * The clamped `priv_out` is what ehem_x25519_shared() expects.
 *
 * Returns EHEM_ERR_ARG on a NULL seed; EHEM_ERR_PROTOCOL if the crypto engine
 * unexpectedly rejects the inputs.
 */
ehem_rc ehem_x25519_keypair_from_seed(const uint8_t seed[EHEM_X25519_KEYSIZE],
                                      uint8_t priv_out[EHEM_X25519_KEYSIZE],
                                      uint8_t pub_out[EHEM_X25519_KEYSIZE]);

/*
 * X25519 ECDH: out = X25519(priv, peer_pub), 32 bytes little-endian. `priv` is
 * a scalar (clamped or not — X25519 clamps internally); `peer_pub` is the peer
 * public key / u-coordinate.
 *
 * Returns EHEM_ERR_ARG on a NULL buffer; EHEM_ERR_PROTOCOL if the engine
 * rejects the peer key (e.g. a small-order / all-zero point).
 */
ehem_rc ehem_x25519_shared(const uint8_t priv[EHEM_X25519_KEYSIZE],
                           const uint8_t peer_pub[EHEM_X25519_KEYSIZE],
                           uint8_t out[EHEM_X25519_KEYSIZE]);

/*
 * Zeroize `n` bytes at `p` in a way the compiler may not elide (used to scrub
 * key material, KDF output, and shared secrets). NULL-safe (no-op when p is
 * NULL). Not a constant-time compare — purely a scrub.
 */
void ehem_zeroize(void *p, size_t n);

/* --------------------------------------------------------------------------
 * X.509 leaf inspection — REQ-SYS-006 harvest / REQ-TOOL-003 skip-if-current.
 *
 * The check-in cert-install path needs three facts about the cloud-delivered
 * certificate: its serial (to compare with the device's currently-loaded one),
 * and its validity window (for the summary). Rather than hand-roll an ASN.1
 * walk we lean on wolfCrypt's cert decoder — the same containment rule as the
 * rest of the shim, so no ASN.1/wolfSSL type escapes into a public header.
 * -------------------------------------------------------------------------- */

#define EHEM_CERT_SERIAL_HEX_CAP 67  /* 33 serial bytes * 2 + NUL (generous) */
#define EHEM_CERT_CN_CAP         128 /* subject CN, truncated if longer */
#define EHEM_CERT_DATE_CAP       21  /* "YYYY-MM-DDTHH:MM:SSZ" + NUL */

typedef struct ehem_cert_fields {
    char serial_hex[EHEM_CERT_SERIAL_HEX_CAP];  /* uppercase hex, no separators;
                                                 * a single leading 0x00 DER
                                                 * sign byte is dropped so the
                                                 * value is convention-stable */
    char subject_cn[EHEM_CERT_CN_CAP];          /* "" if the cert carries none */
    char not_before[EHEM_CERT_DATE_CAP];        /* UTC ISO-8601 (…Z); "" if
                                                 * the date could not be read */
    char not_after[EHEM_CERT_DATE_CAP];
} ehem_cert_fields;

/*
 * Parse the leaf certificate at the START of `der` (the first cert of a
 * concatenated DER chain). On success fills *out and returns EHEM_OK. Returns
 * EHEM_ERR_ARG on a NULL/empty argument, EHEM_ERR_PROTOCOL if the bytes do not
 * parse as an X.509 certificate. Structure only — no signature/date validation.
 */
ehem_rc ehem_cert_parse_leaf(const uint8_t *der, size_t der_len,
                             ehem_cert_fields *out);

/*
 * Format raw serial-number bytes as an uppercase hex string in `out` (capacity
 * `cap`, always NUL-terminated). A single leading 0x00 DER sign byte is dropped
 * so the two producers of a serial (this shim's cert parse, and the device's
 * base64 `csn` claim decoded by the caller) yield the SAME string for the same
 * certificate regardless of sign-byte convention — that equality is what
 * skip-if-current relies on (REQ-TOOL-003).
 */
void ehem_serial_hex(const uint8_t *serial, size_t len, char *out, size_t cap);

/* --------------------------------------------------------------------------
 * Local signature verification — the M4 gate criterion (REQ-OPS-001): a
 * signature produced via ehem_sign() must verify locally with wolfCrypt.
 *
 * Inputs are exactly what the device puts on the wire: the public key as
 * ehem_key_get() returns it (NIST curves: an X9.63 point — the firmware
 * exports COMPRESSED form, and uncompressed is accepted too; Ed25519: raw
 * 32 bytes) and the signature as ehem_sign() returns it (NIST ECDSA: DER
 * ECDSA-Sig-Value over the message hashed with the curve's paired digest —
 * SHA-256/384/512 for 256/384/521-bit curves, mirroring the device's alg
 * table; Ed25519: raw 64 bytes, pure RFC 8032).
 *
 * Both functions distinguish "the crypto ran and the signature does not
 * match" (EHEM_OK with *valid_out = 0) from "the inputs could not be
 * processed" (EHEM_ERR_PROTOCOL: unparseable point/signature, engine
 * failure). EHEM_ERR_ARG on NULL buffers (msg may be NULL only when
 * msg_len is 0) or, for Ed25519, a sig_len other than 64.
 * -------------------------------------------------------------------------- */

/* NIST curves the exdsa endpoint signs with (device vocabulary order). */
typedef enum ehem_ecdsa_curve {
    EHEM_ECDSA_SECP256R1 = 0,   /* pairs with SHA-256 ("SHA256WithECDSA") */
    EHEM_ECDSA_SECP384R1,       /* pairs with SHA-384 */
    EHEM_ECDSA_SECP521R1,       /* pairs with SHA-512 */
    EHEM_ECDSA_SECP256K1        /* pairs with SHA-256 */
} ehem_ecdsa_curve;

#define EHEM_ED25519_PUB_SIZE 32
#define EHEM_ED25519_SIG_SIZE 64

ehem_rc ehem_ecdsa_verify(ehem_ecdsa_curve curve,
                          const uint8_t *pub_x963, size_t pub_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig_der, size_t sig_len,
                          int *valid_out);

ehem_rc ehem_ed25519_verify(const uint8_t pub[EHEM_ED25519_PUB_SIZE],
                            const uint8_t *msg, size_t msg_len,
                            const uint8_t *sig, size_t sig_len,
                            int *valid_out);

/*
 * Process-global wolfCrypt init/cleanup (REQ-API-002). Idempotency is the
 * caller's (ehem_global_init/cleanup); these run the raw wolfCrypt_Init /
 * wolfCrypt_Cleanup step, same contract as the transport backend pair.
 *
 * wolfCrypt_Init() is REQUIRED before any wolfCrypt call on platforms whose
 * mutexes lack a static initializer (Windows CRITICAL_SECTION): the X25519
 * path runs the wolfCrypt RNG (curve25519 blinding, default-on since wolfSSL
 * 5.8.2), and locking its uninitialized global mutex crashes. pthread builds
 * initialize those mutexes statically, which masks a missing init on Linux.
 */
ehem_rc ehem_crypto_backend_global_init(void);
void ehem_crypto_backend_global_cleanup(void);

#endif /* EHEM_CRYPTO_SHIM_H */
