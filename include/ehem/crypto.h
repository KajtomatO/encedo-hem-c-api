/*
 * crypto.h — Encedo HEM C SDK, cryptographic-operation protocol bindings.
 *
 * implements: REQ-OPS-001
 *
 * First of the `crypto` API group (ARCHITECTURE.md §6, §11 M4): single-shot
 * signing over POST /api/crypto/exdsa/sign. The device is a network signing
 * module — the private key never leaves it; the SDK sends the message and
 * receives the signature.
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_CRYPTO_H */
