/*
 * ejwt.h — base64 codecs + compact eJWT builder for the login flow.
 *
 * implements: REQ-AUTH-001 (the eJWT the passphrase login POSTs to
 *             /api/auth/token — ARCHITECTURE.md §5)
 *
 * INTERNAL header — not shipped in include/ehem/, never part of the public ABI.
 *
 * Two base64 conventions the HEM protocol mixes (kept as distinct named pairs,
 * matching the python client's _base64.py):
 *   - standard base64 WITH padding  — API fields and the eJWT `iss` claim;
 *   - base64url WITHOUT padding      — the three eJWT segments.
 */
#ifndef EHEM_EJWT_H
#define EHEM_EJWT_H

#include <stddef.h>
#include <stdint.h>

#include <ehem/ehem.h>   /* ehem_rc */

/* Exact encoded length (chars, excluding the NUL terminator) for `n` input
 * bytes, for buffer sizing. */
size_t ehem_b64_std_encoded_len(size_t n);   /* standard, padded   */
size_t ehem_b64url_encoded_len(size_t n);     /* base64url, no pad  */

/*
 * Encode `in_len` bytes to a NUL-terminated string in `out` (capacity `out_cap`
 * including the NUL). Returns the string length (excl. NUL), or (size_t)-1 if
 * the arguments are invalid or `out_cap` is too small. `in` may be NULL only
 * when in_len is 0.
 */
size_t ehem_b64_std_encode(const uint8_t *in, size_t in_len,
                           char *out, size_t out_cap);
size_t ehem_b64url_encode(const uint8_t *in, size_t in_len,
                          char *out, size_t out_cap);

/*
 * Decode `in_len` base64 chars into `out` (capacity `out_cap`); returns the
 * number of bytes written, or (size_t)-1 on invalid input (a character outside
 * the variant's alphabet, an impossible length, or insufficient capacity). The
 * decoded byte count never exceeds in_len, so out_cap >= in_len always fits.
 * Trailing '=' padding is accepted (and required-length-agnostic); the std
 * decoder rejects '-'/'_' and the url decoder rejects '+'/'/'.
 */
size_t ehem_b64_std_decode(const char *in, size_t in_len,
                           uint8_t *out, size_t out_cap);
size_t ehem_b64url_decode(const char *in, size_t in_len,
                          uint8_t *out, size_t out_cap);

/*
 * Build the compact eJWT for a login challenge (REQ-AUTH-001). Mirrors the
 * python client's build_ejwt boundary, except the passphrase-derived material
 * (shared secret + user public key) is produced by the crypto shim and passed
 * in rather than derived here.
 *
 *   jti           challenge.jti           → "jti" claim (string)
 *   spk           challenge.spk           → "aud" claim (verbatim passthrough)
 *   scope         requested scope         → "scope" claim
 *   user_pub[32]  user X25519 public key  → "iss" (standard base64, padded)
 *   shared[32]    ECDH shared secret      → HMAC-SHA256 key
 *   now           current unix time       → "iat" claim
 *   requested_exp now + desired lifetime  → "exp" claim (verbatim)
 *
 * The "exp" claim is `requested_exp` as given — it is NOT capped at the
 * challenge's `exp`. The challenge `exp` is the response DEADLINE (enforced by
 * the time-based `jti` nonce server-side), not a token-lifetime bound; the
 * device copies our `exp` into the issued bearer, so capping it produced
 * ~60 s tokens and re-login per request (STEP-M2-045). The caller passes
 * `now + lifetime`, which keeps the eJWT itself un-expired at the device.
 *
 * The header is the fixed byte string {"ecdh":"x25519","alg":"HS256","typ":"JWT"}
 * (base64url, no pad); claims are emitted compactly in order
 * jti/aud/exp/iat/iss/scope; the signature is HMAC-SHA256 over
 * "<header>.<payload>". On success *out_ejwt is a freshly allocated,
 * NUL-terminated token — free it with ehem_ejwt_free(). Returns EHEM_ERR_ARG on
 * a NULL argument, EHEM_ERR_NOMEM on allocation failure, EHEM_ERR_PROTOCOL if a
 * crypto step fails.
 */
ehem_rc ehem_ejwt_build(const char *jti, const char *spk,
                        const char *scope,
                        const uint8_t user_pub[32], const uint8_t shared[32],
                        int64_t now, int64_t requested_exp,
                        char **out_ejwt);

/* Free a token returned by ehem_ejwt_build. NULL-safe. */
void ehem_ejwt_free(char *ejwt);

#endif /* EHEM_EJWT_H */
