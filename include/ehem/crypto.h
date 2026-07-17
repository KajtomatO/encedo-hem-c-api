/*
 * crypto.h — Encedo HEM C SDK, cryptographic-operation protocol bindings.
 *
 * implements: REQ-OPS-001, REQ-OPS-002, REQ-OPS-003, REQ-OPS-004,
 *             REQ-OPS-005, REQ-OPS-006, REQ-OPS-007, REQ-OPS-008
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

/*
 * AES cipher selectors — the device's literal vocabulary, EXACTLY 10 chars
 * (any other length is the device's 400; the SDK pre-validates only the
 * length and passes the string verbatim, REQ-OPS-006).
 */
#define EHEM_CIPHER_ALG_AES128_ECB "AES128-ECB"
#define EHEM_CIPHER_ALG_AES192_ECB "AES192-ECB"
#define EHEM_CIPHER_ALG_AES256_ECB "AES256-ECB"
#define EHEM_CIPHER_ALG_AES128_CBC "AES128-CBC"
#define EHEM_CIPHER_ALG_AES192_CBC "AES192-CBC"
#define EHEM_CIPHER_ALG_AES256_CBC "AES256-CBC"
#define EHEM_CIPHER_ALG_AES128_GCM "AES128-GCM"
#define EHEM_CIPHER_ALG_AES192_GCM "AES192-GCM"
#define EHEM_CIPHER_ALG_AES256_GCM "AES256-GCM"

/* Device cipher limits (fw v1.2.2). The IV is 16 bytes even for GCM (the
 * usual GCM default is 12 — the firmware uses a full AES block). */
#define EHEM_CIPHER_MSG_MAX      2048  /* encrypt plaintext, decoded */
#define EHEM_CIPHER_CT_MAX       2064  /* decrypt ciphertext (+1 CBC pad block) */
#define EHEM_CIPHER_AAD_MAX      16    /* GCM additional authenticated data */
#define EHEM_CIPHER_IV_LEN       16
#define EHEM_CIPHER_TAG_LEN      16
#define EHEM_CIPHER_HKDF_CTX_MAX 64    /* ECDH-derived flow HKDF info suffix */

/*
 * Encrypt output. `iv` is present (has_iv) for CBC and GCM — the device
 * ALWAYS generates it, callers cannot supply one — and absent for ECB;
 * `tag` (has_tag) is GCM-only. Caller-owned; ehem_ciphertext_free().
 */
typedef struct ehem_ciphertext {
    uint8_t *ciphertext;
    size_t   ciphertext_len;
    int      has_iv;
    uint8_t  iv[EHEM_CIPHER_IV_LEN];
    int      has_tag;
    uint8_t  tag[EHEM_CIPHER_TAG_LEN];
} ehem_ciphertext;

/* Decrypt output; the bytes are ZEROIZED on free. */
typedef struct ehem_plaintext {
    uint8_t *plaintext;
    size_t   plaintext_len;
} ehem_plaintext;

/*
 * Encrypt with a device key: POST /api/crypto/cipher/encrypt (REQ-OPS-006).
 *
 * Key flows (as ehem_hmac): DIRECT — `kid` names an AES key; the alg's
 * width must be ≤ the stored key's width (fw v1.2.2 truncates: an AES-256
 * key serves AES128-*; an AES-128 key with AES256-* is the device's 406).
 * ECDH-DERIVED — exactly one of ext_kid/pubkey (formats as ehem_ecdh); the
 * AES key is HKDF-SHA256(ECDH secret, info = "encedo-aes" ‖ hkdf_ctx) —
 * the info PREFIX is fixed firmware-side (crypto.c:58; the API doc's
 * `"encedo"` default is wrong, REQ-OPS-006); `hkdf_ctx` (≤ 64 bytes)
 * appends to it, NULL appends nothing.
 *
 * Mode mechanics (device-side): ECB — msg must be a 16-multiple (else 400),
 * no IV, no integrity; CBC — PKCS#7 added by the device, any msg length;
 * GCM — no padding, optional `aad` (≤ 16 bytes), 16-byte tag returned.
 * The IV is always device-generated (hardware RNG) and returned for
 * CBC/GCM; requests never carry one.
 *
 * Scope: exact "keymgmt:use:<kid>". Errors: 403 → EHEM_ERR_SCOPE_DENIED;
 * 400/406 → EHEM_ERR_DEVICE with detail. EHEM_ERR_ARG with no I/O on:
 * NULL ctx/kid/alg/msg/out, malformed kid/peer args, strlen(alg) != 10,
 * msg_len outside 1..EHEM_CIPHER_MSG_MAX, aad_len > EHEM_CIPHER_AAD_MAX
 * (or NULL aad with nonzero length), hkdf_ctx_len > EHEM_CIPHER_HKDF_CTX_MAX
 * (or NULL hkdf_ctx with nonzero length).
 *
 * On success writes *out (caller frees with ehem_ciphertext_free()).
 */
EHEM_API ehem_rc ehem_encrypt(ehem_ctx *ctx, const char *kid, const char *alg,
                              const uint8_t *msg, size_t msg_len,
                              const uint8_t *aad, size_t aad_len,
                              const char *ext_kid,
                              const uint8_t *pubkey, size_t pubkey_len,
                              const uint8_t *hkdf_ctx, size_t hkdf_ctx_len,
                              ehem_ciphertext **out);

/* Release ciphertext from ehem_encrypt(). NULL is a no-op. */
EHEM_API void ehem_ciphertext_free(ehem_ciphertext *c);

/*
 * Decrypt with a device key: POST /api/crypto/cipher/decrypt (REQ-OPS-006).
 * Mirror of ehem_encrypt(): same flows, same alg vocabulary. `iv` (CBC/GCM)
 * and `tag` (GCM) must be exactly EHEM_CIPHER_IV_LEN/EHEM_CIPHER_TAG_LEN
 * bytes when given (iv_len/tag_len; NULL/0 omits — the SDK rejects any
 * other length, EHEM_ERR_ARG). `msg` is the ciphertext, 1..
 * EHEM_CIPHER_CT_MAX bytes. The device strips CBC PKCS#7 padding; a GCM
 * tag/aad mismatch is the device's 406 → EHEM_ERR_DEVICE.
 *
 * On success writes *out (caller frees with ehem_plaintext_free(), which
 * zeroizes). A zero-length plaintext (external all-padding CBC input) is
 * legal: plaintext_len 0.
 */
EHEM_API ehem_rc ehem_decrypt(ehem_ctx *ctx, const char *kid, const char *alg,
                              const uint8_t *msg, size_t msg_len,
                              const uint8_t *iv, size_t iv_len,
                              const uint8_t *tag, size_t tag_len,
                              const uint8_t *aad, size_t aad_len,
                              const char *ext_kid,
                              const uint8_t *pubkey, size_t pubkey_len,
                              const uint8_t *hkdf_ctx, size_t hkdf_ctx_len,
                              ehem_plaintext **out);

/* Release (and zeroize) plaintext from ehem_decrypt(). NULL is a no-op. */
EHEM_API void ehem_plaintext_free(ehem_plaintext *p);

/* ML-KEM sizes (FIPS 203, fw v1.2.2): the shared secret is always 32; the
 * ciphertext is 768/1088/1568 by parameter set (the key decides the set). */
#define EHEM_MLKEM_SS_LEN 32
#define EHEM_MLKEM_CT_MAX 1568

/* Parameter-set name buffer for the PQC responses ("MLKEM768", "MLDSA65",
 * ...). May be EMPTY: the field is informational and, on mlkem/decaps, fw
 * v1.2.2 echoes an uninitialized buffer (REQ-OPS-007) — the SDK stores a
 * best-effort truncated copy and never interprets it. */
#define EHEM_PQC_ALG_SIZE 16

/*
 * ML-KEM encapsulation output: a fresh 32-byte shared secret (zeroized on
 * free) + the ciphertext for the peer + the key's parameter set. Caller-
 * owned; release with ehem_mlkem_encaps_result_free().
 */
typedef struct ehem_mlkem_encaps_result {
    char     alg[EHEM_PQC_ALG_SIZE];
    uint8_t  ss[EHEM_MLKEM_SS_LEN];
    uint8_t *ct;
    size_t   ct_len;
} ehem_mlkem_encaps_result;

/* ML-KEM decapsulation output (ss zeroized on free). */
typedef struct ehem_mlkem_secret {
    char    alg[EHEM_PQC_ALG_SIZE];
    uint8_t ss[EHEM_MLKEM_SS_LEN];
} ehem_mlkem_secret;

/*
 * ML-KEM encapsulate against the device key `kid`:
 * POST /api/crypto/pqc/mlkem/encaps (REQ-OPS-007). The body is {kid} only —
 * the parameter set is fixed by the key. NOTE the shared secret crosses the
 * wire (inside TLS) by the endpoint's design; the SDK zeroizes its struct
 * copy on free. Scope: exact "keymgmt:use:<kid>". Errors: 403 →
 * EHEM_ERR_SCOPE_DENIED; 400/406 (not ML-KEM / not found / crypto failure)
 * → EHEM_ERR_DEVICE. EHEM_ERR_ARG (no I/O): NULL ctx/kid/out, malformed kid.
 */
EHEM_API ehem_rc ehem_mlkem_encaps(ehem_ctx *ctx, const char *kid,
                                   ehem_mlkem_encaps_result **out);

/* Release (ss zeroized) an encaps result. NULL is a no-op. */
EHEM_API void ehem_mlkem_encaps_result_free(ehem_mlkem_encaps_result *r);

/*
 * ML-KEM decapsulate `ct` with the device key `kid`:
 * POST /api/crypto/pqc/mlkem/decaps (REQ-OPS-007). The device requires
 * ct_len to match the key's set EXACTLY (768/1088/1568 — a mismatch is its
 * 406); the SDK pre-validates only 1..EHEM_MLKEM_CT_MAX (it does not know
 * the key's set). The response `alg` is unreliable on fw v1.2.2 (see
 * EHEM_PQC_ALG_SIZE). On success writes *out (free with
 * ehem_mlkem_secret_free(), which zeroizes).
 */
EHEM_API ehem_rc ehem_mlkem_decaps(ehem_ctx *ctx, const char *kid,
                                   const uint8_t *ct, size_t ct_len,
                                   ehem_mlkem_secret **out);

/* Release (and zeroize) a decaps secret. NULL is a no-op. */
EHEM_API void ehem_mlkem_secret_free(ehem_mlkem_secret *s);

/* ML-DSA signature sizes (FIPS 204): 2420/3309/4627 by set. */
#define EHEM_MLDSA_SIG_MAX 4627

/* An ML-DSA signature + the key's parameter set ("MLDSA44/65/87").
 * Caller-owned; release with ehem_mldsa_signature_free(). */
typedef struct ehem_mldsa_signature {
    char     alg[EHEM_PQC_ALG_SIZE];
    uint8_t *sig;
    size_t   sig_len;
} ehem_mldsa_signature;

/*
 * ML-DSA sign: POST /api/crypto/pqc/mldsa/sign (REQ-OPS-008). `msg` is the
 * full message (1..EHEM_SIGN_MSG_MAX — the device hashes internally);
 * `sig_ctx` is the optional FIPS 204 context (≤ EHEM_SIGN_SIG_CTX_MAX
 * bytes, NULL omits) — a signature made with a ctx verifies only with the
 * same ctx. No client-side `alg` selector exists: the key's set decides and
 * the response reports it. Scope/errors as ehem_mlkem_encaps.
 */
EHEM_API ehem_rc ehem_mldsa_sign(ehem_ctx *ctx, const char *kid,
                                 const uint8_t *msg, size_t msg_len,
                                 const uint8_t *sig_ctx, size_t sig_ctx_len,
                                 ehem_mldsa_signature **out);

/* Release an ML-DSA signature. NULL is a no-op. */
EHEM_API void ehem_mldsa_signature_free(ehem_mldsa_signature *sig);

/*
 * ML-DSA verify: POST /api/crypto/pqc/mldsa/verify (REQ-OPS-008). EHEM_OK
 * exactly when the device reports the signature valid (empty-body 200).
 *
 * FIRMWARE QUIRK (fw v1.2.2): a FAILED verification does not produce the
 * documented 406 — the handler passes the crypto layer's raw failure code
 * into the HTTP status line (api_crypto.c:2545), yielding an out-of-range
 * status. The SDK therefore maps ANY completed non-200 response that is not
 * an auth failure (401/403) to EHEM_ERR_DEVICE with the raw status
 * retrievable via ehem_last_error() — including statuses outside 100..599.
 * Genuine transport failures keep their EHEM_ERR_NETWORK/UNREACHABLE codes.
 */
EHEM_API ehem_rc ehem_mldsa_verify(ehem_ctx *ctx, const char *kid,
                                   const uint8_t *msg, size_t msg_len,
                                   const uint8_t *sig_ctx, size_t sig_ctx_len,
                                   const uint8_t *sig, size_t sig_len);

/*
 * Fill `buf` with `len` bytes of DEVICE hardware-RNG output (REQ-OPS-002).
 *
 * fw v1.2.2 has no random endpoint; the only reachable device-RNG source is
 * the fresh 16-byte IV `cipher/encrypt` generates on every call. This
 * routine performs ⌈len/16⌉ AES128-CBC encryptions of a throwaway
 * single-zero-byte payload with the caller-designated AES key `kid`
 * (AES128-* works with ANY stored AES key width) and concatenates the
 * returned IVs; the ciphertexts are discarded. COST: one authenticated
 * round-trip per 16 bytes — budget accordingly (REQ-NET-006 pacing applies
 * between requests when configured).
 *
 * `kid` must name an existing AES key; the SDK never creates or deletes
 * keys implicitly (hem-tool `random` orchestrates a transient key when the
 * caller has none). Scope: exact "keymgmt:use:<kid>" (one cached token
 * serves all round-trips). Errors map as ehem_encrypt; EHEM_ERR_ARG with
 * no I/O on NULL ctx/kid/buf, malformed kid, or len 0.
 */
EHEM_API ehem_rc ehem_random(ehem_ctx *ctx, const char *kid,
                             uint8_t *buf, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_CRYPTO_H */
