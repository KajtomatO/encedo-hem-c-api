/*
 * crypto.h — Encedo HEM C SDK, cryptographic-operation protocol bindings.
 *
 * implements: REQ-OPS-001, REQ-OPS-003, REQ-OPS-004, REQ-OPS-005
 *
 * The `crypto` API group (ARCHITECTURE.md §6, §11 M4/M6): single-shot
 * operations against keys that never leave the device — the SDK sends the
 * inputs and receives the result. Every endpoint here authenticates with the
 * EXACT per-key scope "keymgmt:use:<kid>" (firmware strcmp; one cached token
 * per key serves get + every crypto op on that key).
 */
#ifndef EHEM_CRYPTO_H
#define EHEM_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Algorithm selectors — the device's literal `alg` vocabulary (firmware
 * rejects any other string). The ECDSA selectors are fixed digest/curve
 * pairs: SHA-256 for the 256-bit curves (SECP256R1/SECP256K1), SHA-384 for
 * SECP384R1, SHA-512 for SECP521R1. The Ed* selectors are the RFC 8032
 * variants; `ph` (pre-hash) and `ctx` take the signature context parameter.
 */
#define EHEM_SIGN_ALG_SHA256_ECDSA "SHA256WithECDSA"
#define EHEM_SIGN_ALG_SHA384_ECDSA "SHA384WithECDSA"
#define EHEM_SIGN_ALG_SHA512_ECDSA "SHA512WithECDSA"
#define EHEM_SIGN_ALG_ED25519      "Ed25519"
#define EHEM_SIGN_ALG_ED25519PH    "Ed25519ph"
#define EHEM_SIGN_ALG_ED25519CTX   "Ed25519ctx"
#define EHEM_SIGN_ALG_ED448        "Ed448"
#define EHEM_SIGN_ALG_ED448PH      "Ed448ph"

/* Device limits (fw v1.2.2): message 1..2048 bytes decoded — zero-length is
 * rejected by the firmware's signer — and the RFC 8032 context ≤ 255 bytes. */
#define EHEM_SIGN_MSG_MAX     2048
#define EHEM_SIGN_SIG_CTX_MAX 255

/*
 * A signature as the device produced it, byte-for-byte (REQ-OPS-001):
 *   - ECDSA selectors → DER-encoded ECDSA-Sig-Value, variable length (the
 *     per-curve maximum is ehem_key_type_info.sig_max_len);
 *   - Ed* selectors  → raw RFC 8032 bytes (Ed25519 64, Ed448 114).
 * Conversion to other spellings (fixed r‖s for PKCS#11) is the caller's.
 * Caller-owned; release with ehem_signature_free().
 */
typedef struct ehem_signature {
    uint8_t *sig;
    size_t   sig_len;
} ehem_signature;

/*
 * Sign `msg` with the device key `kid`: POST /api/crypto/exdsa/sign.
 *
 * The call authenticates with the EXACT per-key scope "keymgmt:use:<kid>" —
 * the same scope ehem_key_get() uses, so one cached token per key serves
 * both (firmware v1.2.2 matches the sign scope with strcmp: no broader
 * scope is accepted, and Manager-issued tokens — sub "M" — are rejected).
 *
 * The device hashes internally in every variant: `msg` is the full message
 * (1..EHEM_SIGN_MSG_MAX bytes), NOT a digest. A consumer holding only a
 * pre-computed digest (PKCS#11 CKM_ECDSA) must arrange its flow around that.
 *
 * `sig_ctx`/`sig_ctx_len` is the optional RFC 8032 context for the
 * Ed25519ctx/Ed25519ph/Ed448/Ed448ph selectors (≤ EHEM_SIGN_SIG_CTX_MAX
 * bytes); pass NULL/0 to omit the field. `alg` is sent verbatim — the SDK
 * keeps no allowlist (an unknown selector is the device's 400 to report).
 *
 * Returns EHEM_ERR_ARG with no network I/O on: NULL ctx/kid/alg/msg/out, a
 * kid that is not exactly 32 hex chars, an empty alg, msg_len outside
 * 1..EHEM_SIGN_MSG_MAX, sig_ctx_len > EHEM_SIGN_SIG_CTX_MAX, or a NULL
 * sig_ctx with a nonzero length. 401/403 map per REQ-AUTH-003 (403 →
 * EHEM_ERR_SCOPE_DENIED). A device 400 (malformed field / unknown alg) and
 * 406 are EHEM_ERR_DEVICE with detail in ehem_last_error() — 406 is
 * deliberately NOT mapped to EHEM_ERR_NOT_FOUND because the firmware returns
 * it indistinguishably for "kid not found", "wrong key type for alg", and
 * low-level signing failure.
 *
 * On success writes *out (caller frees with ehem_signature_free()).
 */
EHEM_API ehem_rc ehem_sign(ehem_ctx *ctx, const char *kid, const char *alg,
                           const uint8_t *msg, size_t msg_len,
                           const uint8_t *sig_ctx, size_t sig_ctx_len,
                           ehem_signature **out);

/* Release a signature from ehem_sign(). NULL is a no-op. */
EHEM_API void ehem_signature_free(ehem_signature *sig);

/* Largest signature the verify endpoint accepts, decoded (fw v1.2.2:
 * 2·66+16 — fits the DER-encoded P-521 ECDSA maximum and every EdDSA size). */
#define EHEM_VERIFY_SIG_MAX 148

/*
 * Verify a signature with the device key `kid`: POST /api/crypto/exdsa/verify
 * (REQ-OPS-003). Mirror of ehem_sign(): same scope ("keymgmt:use:<kid>",
 * shared cached token), same `alg` vocabulary passed verbatim, same
 * internal-hashing rule (`msg` is the full message, not a digest), same
 * optional RFC 8032 `sig_ctx`. `sig`/`sig_len` is the signature exactly as
 * ehem_sign() produced it (DER for ECDSA, raw for EdDSA).
 *
 * Returns EHEM_OK exactly when the device reports the signature VALID (an
 * empty-body 200). There is no boolean output: any other outcome is "not
 * verified", with detail in ehem_last_error(). The device's 406 covers
 * invalid signature, wrong key type for `alg`, and kid-not-found
 * indistinguishably (firmware CRYPTO_SignVerify: any failure → 406) →
 * EHEM_ERR_DEVICE; 400 → EHEM_ERR_DEVICE; 403 → EHEM_ERR_SCOPE_DENIED.
 *
 * EHEM_ERR_ARG with no network I/O on: NULL ctx/kid/alg/msg/sig, kid not 32
 * hex chars, empty alg, msg_len outside 1..EHEM_SIGN_MSG_MAX, sig_len outside
 * 1..EHEM_VERIFY_SIG_MAX, sig_ctx_len > EHEM_SIGN_SIG_CTX_MAX, or a NULL
 * sig_ctx with nonzero length.
 */
EHEM_API ehem_rc ehem_verify(ehem_ctx *ctx, const char *kid, const char *alg,
                             const uint8_t *msg, size_t msg_len,
                             const uint8_t *sig_ctx, size_t sig_ctx_len,
                             const uint8_t *sig, size_t sig_len);

/*
 * Hash selectors shared by the ECDH and HMAC endpoints — the device's literal
 * vocabulary, passed verbatim.
 */
#define EHEM_HASH_ALG_SHA2_256 "SHA2-256"
#define EHEM_HASH_ALG_SHA2_384 "SHA2-384"
#define EHEM_HASH_ALG_SHA2_512 "SHA2-512"
#define EHEM_HASH_ALG_SHA3_256 "SHA3-256"
#define EHEM_HASH_ALG_SHA3_384 "SHA3-384"
#define EHEM_HASH_ALG_SHA3_512 "SHA3-512"

/* Largest raw peer public key the crypto endpoints accept (P-521 compressed
 * x963: 66-byte curve size + 1 parity byte). */
#define EHEM_ECDH_PUBKEY_MAX 67

/*
 * An ECDH shared secret (or its hash) from ehem_ecdh(). Caller-owned; release
 * with ehem_ecdh_secret_free(), which ZEROIZES the bytes before freeing.
 */
typedef struct ehem_ecdh_secret {
    uint8_t *secret;
    size_t   secret_len;
} ehem_ecdh_secret;

/*
 * Raw ECDH between the device key `kid` and a peer: POST /api/crypto/ecdh
 * (REQ-OPS-004). Nothing is stored on the device — the shared bytes come back
 * to the caller (contrast /api/keymgmt/derive, M7).
 *
 * `kid` must name an ECDH-capable private key (Curve25519/Curve448, or a
 * NIST-P / secp256k1 key created with a mode containing "ECDH"). The peer is
 * EXACTLY ONE of:
 *   - `ext_kid`             — a key already on the device (public half used);
 *                             must be the same family as `kid`;
 *   - `pubkey`/`pubkey_len` — a caller-supplied raw public key: NIST curves
 *                             take COMPRESSED x963 of exactly curve_size+1
 *                             bytes (33/49/67/33 — the format ehem_key_get()
 *                             returns), X25519/X448 take raw little-endian
 *                             32/56 bytes. Wrong length/format is the
 *                             device's 406.
 * Pass NULL / (NULL,0) for the unused one; both or neither → EHEM_ERR_ARG.
 *
 * `alg` (optional, NULL omits): one of the EHEM_HASH_ALG_* literals — the
 * device then returns that hash of the shared secret instead of the raw
 * bytes. CAUTION, raw mode (fw v1.2.2): the firmware reports the raw secret
 * with a FIXED 32-byte length — for curves whose shared secret is longer
 * (P-384: 48, P-521: 66, X448: 56) the returned bytes are TRUNCATED to the
 * first 32 (firmware crypto.c:1679; LIVE-CONFIRMED on fw v1.2.2-DIAG
 * 2026-07-17 — the API doc's "full curve length" is wrong, REQ-OPS-004).
 * The hashed variants digest the FULL secret. Use an alg for >256-bit
 * curves.
 *
 * Scope: exact "keymgmt:use:<kid>" on the primary kid only (shared cached
 * token; no scope is checked on ext_kid). Errors: 403 →
 * EHEM_ERR_SCOPE_DENIED; 400 and 406 (not found / not ECDH-capable / family
 * mismatch / crypto failure, indistinguishable) → EHEM_ERR_DEVICE with
 * detail. EHEM_ERR_ARG with no I/O on: NULL ctx/kid/out, malformed kid or
 * ext_kid, pubkey_len 0 or > EHEM_ECDH_PUBKEY_MAX, a NULL pubkey with
 * nonzero length, an empty alg string, or a peer violation (both/neither).
 *
 * On success writes *out (caller frees with ehem_ecdh_secret_free()).
 */
EHEM_API ehem_rc ehem_ecdh(ehem_ctx *ctx, const char *kid,
                           const char *ext_kid,
                           const uint8_t *pubkey, size_t pubkey_len,
                           const char *alg,
                           ehem_ecdh_secret **out);

/* Release (and zeroize) a secret from ehem_ecdh(). NULL is a no-op. */
EHEM_API void ehem_ecdh_secret_free(ehem_ecdh_secret *s);

/* Largest MAC the hmac endpoints handle (SHA-512/SHA3-512: 64 bytes). */
#define EHEM_HMAC_MAC_MAX 64

/* A MAC from ehem_hmac(). Caller-owned; release with ehem_mac_free(). */
typedef struct ehem_mac {
    uint8_t *mac;
    size_t   mac_len;
} ehem_mac;

/*
 * MAC a message with a device key: POST /api/crypto/hmac/hash (REQ-OPS-005).
 * Two key flows, selected by the peer arguments:
 *
 *   - DIRECT (ext_kid NULL, pubkey NULL/0): `kid` names an HMAC key. The
 *     hash is IMPLIED BY THE KEY'S TYPE and the firmware IGNORES a request
 *     `alg` in this flow (fw v1.2.2 crypto.c:526 overwrites it) — pass
 *     alg=NULL; a given alg is still sent verbatim but has no effect.
 *
 *   - ECDH-DERIVED (exactly one of ext_kid / pubkey, same peer rules and
 *     formats as ehem_ecdh()): `kid` is an ECDH-capable private key; the
 *     RAW ECDH shared secret becomes the HMAC key (fw v1.2.2 applies NO
 *     HKDF here, unlike the cipher endpoints — the API doc's "ECDH + HKDF"
 *     is wrong; see REQ-OPS-005). `alg` (an EHEM_HASH_ALG_* literal) is
 *     REQUIRED in this flow — the SDK pre-validates that (EHEM_ERR_ARG).
 *
 * The MAC length follows the effective hash (32/48/64). Scope: exact
 * "keymgmt:use:<kid>" (shared cached token). Errors: 403 →
 * EHEM_ERR_SCOPE_DENIED; 400/406 (wrong key type, ECDH failure, ...) →
 * EHEM_ERR_DEVICE with detail. EHEM_ERR_ARG with no I/O on: NULL
 * ctx/kid/msg/out, malformed kid/peer args (see ehem_ecdh), msg_len outside
 * 1..EHEM_SIGN_MSG_MAX, an empty alg string, or a derived flow without alg.
 *
 * On success writes *out (caller frees with ehem_mac_free()).
 */
EHEM_API ehem_rc ehem_hmac(ehem_ctx *ctx, const char *kid, const char *alg,
                           const uint8_t *msg, size_t msg_len,
                           const char *ext_kid,
                           const uint8_t *pubkey, size_t pubkey_len,
                           ehem_mac **out);

/* Release a MAC from ehem_hmac(). NULL is a no-op. */
EHEM_API void ehem_mac_free(ehem_mac *m);

/*
 * Verify a MAC: POST /api/crypto/hmac/verify (REQ-OPS-005). Mirror of
 * ehem_hmac() — same key flows, same alg rules — plus the MAC to check
 * (`mac`/`mac_len`, 1..EHEM_HMAC_MAC_MAX bytes). Returns EHEM_OK exactly
 * when the device reports the MAC valid (empty-body 200); a mismatch is the
 * device's 406 → EHEM_ERR_DEVICE (indistinguishable from wrong-key-type and
 * ECDH failure, like the other crypto endpoints).
 */
EHEM_API ehem_rc ehem_hmac_verify(ehem_ctx *ctx, const char *kid,
                                  const char *alg,
                                  const uint8_t *msg, size_t msg_len,
                                  const uint8_t *mac, size_t mac_len,
                                  const char *ext_kid,
                                  const uint8_t *pubkey, size_t pubkey_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_CRYPTO_H */
