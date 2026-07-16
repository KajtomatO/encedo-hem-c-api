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
