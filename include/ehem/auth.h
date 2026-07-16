/*
 * auth.h — Encedo HEM C SDK, passphrase login and session control.
 *
 * implements: REQ-AUTH-001, REQ-AUTH-002
 *
 * The HEM authenticates with a custom eJWT challenge–response: the SDK fetches
 * a per-session challenge, derives the user's X25519 keypair from the
 * passphrase (PBKDF2-HMAC-SHA256, salt = the challenge `eid`), performs ECDH
 * against the device's session key, and submits an HMAC-SHA256-signed compact
 * JWT to obtain a scoped bearer token (ARCHITECTURE.md §5).
 *
 * This header exposes only the two lifecycle calls a consumer makes directly:
 * ehem_login() to establish a session and ehem_logout() to end it. Token
 * acquisition itself is lazy and automatic — the authenticated protocol
 * bindings (added from M2 on) acquire and cache a token for the scope they
 * need on first use, and silently refresh it before it expires. There is no
 * public "get token" call; a bearer token never crosses the public API.
 */
#ifndef EHEM_AUTH_H
#define EHEM_AUTH_H

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Establish an authenticated session on `ctx` using `passphrase` (a
 * NUL-terminated UTF-8 string). This does NOT contact the device: it records
 * the credential and returns. The first authenticated call then performs the
 * challenge→derive→token exchange lazily and caches the resulting bearer token
 * per scope (REQ-AUTH-001, REQ-AUTH-002).
 *
 * By default the passphrase is retained (as a zeroized-on-release copy) so the
 * SDK can silently re-acquire tokens for the context's lifetime. Set
 * ehem_options.no_credential_retention before ehem_ctx_create() to instead keep
 * only the acquired tokens: the passphrase is scrubbed after its first use and
 * a later token refresh fails EHEM_ERR_AUTH_EXPIRED until ehem_login() is
 * called again.
 *
 * Calling ehem_login() again re-logs-in: it drops the token cache and replaces
 * the stored credential. Returns EHEM_ERR_ARG on a NULL ctx/passphrase,
 * EHEM_ERR_NOMEM on allocation failure, otherwise EHEM_OK. Authentication
 * failures (a wrong passphrase, a device that rejects the token) surface later,
 * at the first authenticated call, not here.
 */
EHEM_API ehem_rc ehem_login(ehem_ctx *ctx, const char *passphrase);

/*
 * End the session on `ctx`: zeroize and release the retained passphrase and
 * drop the entire token cache (REQ-AUTH-002). After logout, authenticated
 * calls fail (EHEM_ERR_AUTH_EXPIRED) with no network traffic until the next
 * ehem_login(). ehem_ctx_destroy() performs an implicit logout, so an explicit
 * call is only needed to end a session while keeping the context. Returns
 * EHEM_ERR_ARG on a NULL ctx, otherwise EHEM_OK (a no-op if never logged in).
 */
EHEM_API ehem_rc ehem_logout(ehem_ctx *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_AUTH_H */
