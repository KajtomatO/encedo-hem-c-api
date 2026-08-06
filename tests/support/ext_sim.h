/*
 * ext_sim.h — simulated ExtAuth authenticator (test support).
 *
 * implements: REQ-TEST-006 (device-local ExtAuth testing: everything the
 *             Encedo mobile app does cryptographically, reproduced from
 *             fw api_auth.c so integration tests can pair and login against
 *             the real device with no broker and no phone)
 *
 * TEST SUPPORT — links the static library for shim/codec internals
 * (crypto_shim.h, ejwt.h, json.h); never part of the shipped SDK.
 *
 * The pieces, mirroring the firmware's expectations:
 *   - X25519 keypairs: an "identity" pair (what /ext/validate imports and
 *     /ext/token authenticates) and per-exchange ephemerals. Per-run-unique
 *     generation (time-seeded) avoids the REQ-KEY-008 repo dedup trap.
 *   - Compact HS256 JWTs with the header {"ecdh":"x25519","alg":"HS256",
 *     "typ":"JWT"} (byte-identical to the login eJWT header): build for
 *     reply/authreply, open (verify+parse) for request/authreq when the
 *     test holds the counterpart secret, peek (parse only) otherwise.
 *   - The scheme-A scope codec (fw api_auth.c:1526-1545 encrypt /
 *     :1743-1801 decrypt):
 *       K   = HMAC-SHA256(key = ECDH secret, msg = jti raw 32 bytes)
 *       ct  = AES-128-CBC(key = K[0..15], iv = K[16..31],
 *                         scope padded with pad-byte = pad-count, >= 1)
 *       tag = HMAC-SHA256(key = K (32 bytes), msg = UNPADDED scope)
 *       value = "A" + base64_std(ct || tag)
 *
 * All functions return 0 on success, -1 on failure unless noted.
 */
#ifndef EHEM_EXT_SIM_H
#define EHEM_EXT_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"   /* ehem_json — claim sets in, parsed payloads out */

#define EHEM_SIM_KEY_SIZE 32   /* X25519 scalar/point, HMAC key, jti nonce */

typedef struct ehem_sim_keypair {
    uint8_t priv[EHEM_SIM_KEY_SIZE];   /* clamped scalar */
    uint8_t pub[EHEM_SIM_KEY_SIZE];
} ehem_sim_keypair;

/* Fresh keypair, unique per call (time/counter-seeded through the shim KDF
 * path — uniqueness, not cryptographic randomness, is the requirement). */
int ehem_sim_keypair_gen(ehem_sim_keypair *kp);

/* Deterministic keypair from a fixed seed (fixture vectors). */
int ehem_sim_keypair_from_seed(const uint8_t seed[EHEM_SIM_KEY_SIZE],
                               ehem_sim_keypair *kp);

/* ECDH through the shim: out = X25519(kp->priv, peer_pub). */
int ehem_sim_shared(const ehem_sim_keypair *kp,
                    const uint8_t peer_pub[EHEM_SIM_KEY_SIZE],
                    uint8_t out[EHEM_SIM_KEY_SIZE]);

/* Standard-base64 (padded) helpers for claim values — the convention the
 * firmware uses for iss/epk/jti/pid claim strings (libjwt jwt_Base64encode /
 * wolfSSL Base64_Decode). Encode writes a NUL-terminated malloc'd string. */
char *ehem_sim_b64(const uint8_t *in, size_t len);
/* Decode exactly EHEM_SIM_KEY_SIZE bytes (jti nonces, pids, pubkeys);
 * fails on any other decoded length. */
int ehem_sim_b64_key(const char *b64, uint8_t out[EHEM_SIM_KEY_SIZE]);

/*
 * Build a compact HS256 JWT: header {"ecdh":"x25519","alg":"HS256","typ":
 * "JWT"}, payload = `claims` printed compactly (insertion order), signature
 * HMAC-SHA256("<h>.<p>", key). Returns a malloc'd NUL-terminated token.
 */
int ehem_sim_jwt_build(const ehem_json *claims,
                       const uint8_t key[EHEM_SIM_KEY_SIZE], char **out_jwt);

/*
 * Verify a compact JWT's HS256 signature with `key` and return its parsed
 * payload (caller frees with ehem_json_free). NULL on bad structure, bad
 * base64url, or signature mismatch.
 */
ehem_json *ehem_sim_jwt_open(const char *jwt,
                             const uint8_t key[EHEM_SIM_KEY_SIZE]);

/*
 * Parse a compact JWT's payload WITHOUT verifying the signature — the
 * authenticator cannot verify an authreq (its HMAC key is ECDH(EIDkey, the
 * caller's ephemeral)); it authenticates its own scope entry via the
 * scheme-A trailer instead.
 */
ehem_json *ehem_sim_jwt_peek(const char *jwt);

/*
 * Scheme-A codec. jti/ecdh are RAW 32-byte values (decode claim strings with
 * ehem_sim_b64_key first). Encrypt returns a malloc'd "A…" value string;
 * decrypt returns the malloc'd NUL-terminated plaintext scope (including any
 * "#<meta>" suffix — stripping is the consumer's business) and fails on a
 * missing "A" prefix, bad base64, short input, bad padding, or trailer
 * mismatch.
 */
int ehem_sim_scheme_a_encrypt(const char *scope,
                              const uint8_t jti[EHEM_SIM_KEY_SIZE],
                              const uint8_t ecdh[EHEM_SIM_KEY_SIZE],
                              char **out_value);
int ehem_sim_scheme_a_decrypt(const char *value,
                              const uint8_t jti[EHEM_SIM_KEY_SIZE],
                              const uint8_t ecdh[EHEM_SIM_KEY_SIZE],
                              char **out_scope);

#endif /* EHEM_EXT_SIM_H */
