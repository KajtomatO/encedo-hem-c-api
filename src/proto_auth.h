/*
 * proto_auth.h — internal session engine: login, token cache, ensure-token.
 *
 * implements: REQ-AUTH-001 (the eJWT challenge–response), REQ-AUTH-002 (the
 *             scope-keyed cache with silent refresh + credential retention)
 *
 * INTERNAL header. The public entry points (ehem_login/ehem_logout) live in
 * <ehem/auth.h>; this header adds the pieces other internal components use:
 * ehem_auth_ensure_token() — the chokepoint every authenticated binding will
 * call (M2-040) — and ehem_auth_destroy() for context teardown. All credential
 * state is contained here so the crypto shim and passphrase never leak upward.
 */
#ifndef EHEM_PROTO_AUTH_H
#define EHEM_PROTO_AUTH_H

#include <stdint.h>

#include "context.h"

/*
 * Ensure a valid bearer token for `scope` is available and hand back a borrowed
 * pointer to it in *token_out (owned by the context's cache; valid until the
 * next auth operation on the same context — the caller copies it into an
 * Authorization header immediately and does not retain it).
 *
 * Returns a cached token when one is present and outside the 60 s expiry skew;
 * otherwise runs the REQ-AUTH-001 flow (challenge GET → derive → POST eJWT) and
 * caches the result. With no retained credential (never logged in, logged out,
 * or the passphrase was scrubbed under no_credential_retention) a cache miss is
 * EHEM_ERR_AUTH_EXPIRED with no network traffic. Other failures map per
 * REQ-API-003 (POST 401 → EHEM_ERR_AUTH_FAILED) with ehem_last_error detail.
 * EHEM_ERR_ARG on a NULL argument.
 */
ehem_rc ehem_auth_ensure_token(ehem_ctx *ctx, const char *scope,
                               const char **token_out);

/*
 * Drop the cached token for `scope` (all scopes when `scope` is NULL), scrubbing
 * it. Used by the authenticated request path (REQ-AUTH-003) when the device
 * rejects a token with 401: the next ehem_auth_ensure_token then re-acquires.
 * NULL-safe on ctx / an unlogged-in context (no-op).
 */
void ehem_auth_invalidate(ehem_ctx *ctx, const char *scope);

/*
 * Seed the scope-keyed cache with an externally-acquired bearer (the ExtAuth
 * confirm engine, REQ-AUTH-009). Entry expiry from the bearer's own exp claim
 * minus skew (login-path fallback when unreadable). Creates the auth state if
 * absent — mobile mode holds no passphrase. EHEM_ERR_ARG / EHEM_ERR_NOMEM.
 */
ehem_rc ehem_auth_cache_seed(ehem_ctx *ctx, const char *scope,
                             const char *token);

/*
 * Scrub and free the session state behind `ctx->auth` (zeroizing the retained
 * passphrase and every cached token). NULL-safe. Used by ehem_logout() and
 * ehem_ctx_destroy(); after it returns the pointer must not be reused.
 */
void ehem_auth_destroy(struct ehem_auth *auth);

/*
 * Test-only clock seam (never exported; hidden visibility). When `fn` is
 * non-NULL the auth layer reads "now" from it instead of time(NULL), so unit
 * tests can pin timestamps and craft expiry windows deterministically. Pass
 * NULL to restore the real clock. Not thread-safe — unit-test use only.
 */
void ehem_auth_test_set_clock(int64_t (*fn)(void));

/*
 * Test-only override for the PBKDF2 iteration count (never exported; hidden
 * visibility). When `iters` is non-zero the login derivation uses it instead of
 * the pinned 600 000 rounds, so the cache / retry / bearer tests — which do not
 * depend on the exact derived bytes — run fast (a 600 000-round PBKDF2 is ~0.5 s
 * each, and the suite performs ~20 acquisitions). Pass 0 to restore the
 * production value. The byte-exact fixture test keeps 600 000. Not thread-safe —
 * unit-test use only.
 */
void ehem_auth_test_set_kdf_iters(uint32_t iters);

#endif /* EHEM_PROTO_AUTH_H */
