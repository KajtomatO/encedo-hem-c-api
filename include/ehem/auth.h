/*
 * auth.h — Encedo HEM C SDK, passphrase login, session control, and the
 * ExtAuth (external-authenticator / mobile-app) surface.
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

/* --------------------------------------------------------------------------
 * ExtAuth pairing (REQ-AUTH-006) — register an external authenticator
 * (typically the Encedo mobile app) with the device.
 *
 * Three endpoints, all authenticated with scope "auth:ext:pair" and requiring
 * a passphrase session (`sub`="U" — a mobile-acquired bearer carries the
 * authenticator kid as `sub` and is rejected by these endpoints):
 *
 *   init      the device emits an opaque `request` JWT bound to a
 *             counterparty ephemeral key; the caller forwards it out-of-band
 *             (QR / cloud broker) to the authenticator.
 *   validate  the caller uploads the authenticator's countersigned `reply`;
 *             the device imports the authenticator's Curve25519 public key
 *             into its key repository (descriptor "EXTAID" + the decoded
 *             pid) and returns the new `kid` plus a confirmation `code`
 *             (HMAC-SHA256 of the reply, keyed with ECDH(EIDkey, the
 *             reply's `epk`)) for the authenticator to verify acceptance.
 *   mac       stateless liveness/identity proof toward an already-known
 *             counterparty: a fresh nonce and HMAC-SHA256(nonce,
 *             key=ECDH(EIDkey, epk)). Nothing is stored or consumed
 *             device-side; replay rejection is the counterparty's job.
 *
 * The device caps paired authenticators at 8 slots; a full table (or a
 * re-import of an identical public key — the repo deduplicates) fails with
 * HTTP 406 → EHEM_ERR_DEVICE. Unpairing is ordinary key removal:
 * ehem_key_delete() with the returned kid.
 *
 * All base64 parameters/fields use STANDARD base64 (padded), not base64url.
 * -------------------------------------------------------------------------- */

/* Result of ehem_ext_init(). */
typedef struct ehem_ext_init_info {
    char *request;   /* opaque JWT to forward to the authenticator */
    char *eid;       /* device EncedoID: base64 Curve25519 public key */
} ehem_ext_init_info;

/*
 * Begin pairing: POST /api/auth/ext/init. `epk_b64` is the counterparty's
 * ephemeral Curve25519 public key — standard base64 of exactly 32 bytes,
 * validated client-side (EHEM_ERR_ARG, no network I/O, otherwise the device
 * would 400). No device state changes on this call. On success writes *out
 * (free with ehem_ext_init_free). 401/403 map per REQ-AUTH-003; 409 =
 * device not ready (failure state / not initialised) → EHEM_ERR_DEVICE.
 */
EHEM_API ehem_rc ehem_ext_init(ehem_ctx *ctx, const char *epk_b64,
                               ehem_ext_init_info **out);

/* Release an init result. NULL is a no-op. */
EHEM_API void ehem_ext_init_free(ehem_ext_init_info *info);

/* Result of ehem_ext_validate(). */
typedef struct ehem_ext_validate_info {
    char *kid;    /* hex key id of the imported authenticator key */
    char *code;   /* base64 confirmation code to forward to the authenticator */
} ehem_ext_validate_info;

/*
 * Finalise pairing: POST /api/auth/ext/validate. `pid_b64` is the pairing id
 * — standard base64 of EXACTLY 32 bytes (validated client-side): the device
 * stores the decoded pid as the descriptor suffix and both the authreq scope
 * enumeration and /ext/token read a fixed 32-byte suffix, so any other
 * length pairs a key that can never log in. `reply_jwt` is the
 * authenticator's countersigned reply (which must echo the request's `jti`
 * — a fresh init is needed if it expired). HTTP 406 (slot table full, or
 * duplicate public key) → EHEM_ERR_DEVICE with the ambiguity named in the
 * error detail; 401 also covers a stale/invalid reply JWT.
 */
EHEM_API ehem_rc ehem_ext_validate(ehem_ctx *ctx, const char *pid_b64,
                                   const char *reply_jwt,
                                   ehem_ext_validate_info **out);

/* Release a validate result. NULL is a no-op. */
EHEM_API void ehem_ext_validate_free(ehem_ext_validate_info *info);

/* Result of ehem_ext_mac(). */
typedef struct ehem_ext_mac_info {
    char *nonce;   /* base64 32-byte time nonce */
    char *mac;     /* base64 HMAC-SHA256(nonce, key=ECDH(EIDkey, epk)) */
    char *eid;     /* device EncedoID: base64 Curve25519 public key */
} ehem_ext_mac_info;

/*
 * Liveness/identity proof: POST /api/auth/ext/mac. `epk_b64` as in
 * ehem_ext_init(). On success writes *out (free with ehem_ext_mac_free).
 */
EHEM_API ehem_rc ehem_ext_mac(ehem_ctx *ctx, const char *epk_b64,
                              ehem_ext_mac_info **out);

/* Release a mac result. NULL is a no-op. */
EHEM_API void ehem_ext_mac_free(ehem_ext_mac_info *info);

/* --------------------------------------------------------------------------
 * ExtAuth login (REQ-AUTH-007) — the push-confirm bearer issuance pair.
 *
 * Both endpoints are UNAUTHENTICATED by design (no Bearer parsed): security
 * comes from the ECDH-keyed JWTs. Device preconditions, checked in order:
 * RTC set (else HTTP 403 — the SDK reacts with ONE automatic check-in +
 * retry per call, the REQ-AUTH-004 recovery pattern keyed on 403, unless
 * ehem_options.no_auto_checkin), initialised and failure-state clear (else
 * 409 → EHEM_ERR_DEVICE).
 *
 * Unlike the passphrase flow — where a bearer never crosses the public API —
 * ehem_ext_token() RETURNS the issued bearer: the caller (typically the
 * confirm engine, REQ-AUTH-009, or a consumer driving its own broker) is the
 * one holding the conversation with the authenticator.
 * -------------------------------------------------------------------------- */

/* Result of ehem_ext_request(). */
typedef struct ehem_ext_request_info {
    char *authreq;   /* JWT for the broker/authenticators (opaque to the SDK) */
    char *epk;       /* the caller's epk echoed back (standard base64) */
} ehem_ext_request_info;

/*
 * Ask the device for an `authreq` every paired authenticator can decrypt:
 * POST /api/auth/ext/request. `epk_b64` — a one-shot transport public key
 * (standard base64 of exactly 32 bytes, validated client-side; it need not
 * match any paired authenticator). `scope` — the scope the resulting token
 * shall have, ≤ 1023 bytes. `ctx_str` (1–64 chars) and `note` (1–128 chars)
 * are optional (NULL to omit): `ctx_str` is echoed into the issued token,
 * `note` is free text for the authenticator UI. Out-of-range lengths are
 * EHEM_ERR_ARG client-side (the firmware would silently DROP them).
 *
 * Firmware facts (fw v1.2.2, api_auth.c): a scope of exactly
 * "keymgmt:use:<32-hex-kid>" is rewritten server-side — "#<base64 {type,
 * label}>" is appended (so the phone can show which key) and the token
 * lifetime drops from 60 to 15 minutes. The authreq's `scope` claim is an
 * OBJECT with one encrypted entry per paired authenticator (possibly empty —
 * zero pairings still return 200; /ext/token then has nothing to accept).
 * A request-body `exp` field is IGNORED by the firmware (the hem-api-tester
 * sends one to no effect) — the SDK does not offer it.
 */
EHEM_API ehem_rc ehem_ext_request(ehem_ctx *ctx, const char *epk_b64,
                                  const char *scope, const char *ctx_str,
                                  const char *note,
                                  ehem_ext_request_info **out);

/* Release a request result. NULL is a no-op. */
EHEM_API void ehem_ext_request_free(ehem_ext_request_info *info);

/*
 * Exchange an authenticator's countersigned `authreply` for a bearer:
 * POST /api/auth/ext/token. On success *token_out is the issued bearer JWT
 * (free with ehem_ext_token_free): `sub` = base64 of the approving
 * authenticator's kid (NOT "U"/"M"), `scope` = the approved scope with any
 * "#"-metadata stripped, `exp` = the authreply's own exp claim. On the wire
 * it is indistinguishable from a password-login bearer.
 *
 * 401 (JWT decode/validate/nonce failure — includes an expired or replayed
 * reply) and 406 (unknown authenticator, unsupported scheme, or a scope
 * ciphertext that fails to decrypt/authenticate) both map to
 * EHEM_ERR_AUTH_FAILED with the distinction in the error detail.
 */
EHEM_API ehem_rc ehem_ext_token(ehem_ctx *ctx, const char *authreply_jwt,
                                char **token_out);

/* Release a token returned by ehem_ext_token. NULL is a no-op. */
EHEM_API void ehem_ext_token_free(char *token);

/* --------------------------------------------------------------------------
 * Notification-broker client (REQ-AUTH-008) — the cloud legs that carry the
 * ExtAuth flows to and from phones: `session` (ephemeral key), the
 * registration triple (pairing via QR), and the event pair (push-confirm
 * login).
 *
 * NO documentation exists for this API — every shape here is reconstructed
 * from the Encedo Manager and hem-api-tester and pinned by live probes
 * (broker behavior can change server-side at any time; failures preserve
 * the broker payload in ehem_last_error). All calls are unauthenticated,
 * ALWAYS fully TLS-verified regardless of the context's device trust mode,
 * and take `notify_url` as the broker base (NULL → EHEM_DEFAULT_NOTIFY_URL,
 * the REQ-SYS-013 register_url precedent).
 *
 * The polling calls surface "still pending" (HTTP 202) as EHEM_OK with the
 * result's `pending` flag set — polling cadence and deadlines are the
 * caller's (or the confirm engine's, REQ-AUTH-009); these bindings never
 * sleep.
 * -------------------------------------------------------------------------- */

#define EHEM_DEFAULT_NOTIFY_URL "https://api.encedo.com/notify"

/*
 * Obtain a broker ephemeral Curve25519 public key for one exchange:
 * POST /session {"eid": …} when `eid_b64` is given (the pairing flow — the
 * caller is authenticated device-side and knows the eid), or a bodyless GET
 * when `eid_b64` is NULL (the login flow — deliberately credential-free).
 * On success *epk_out is the base64 key (free with ehem_notify_string_free).
 */
EHEM_API ehem_rc ehem_notify_session(ehem_ctx *ctx, const char *notify_url,
                                     const char *eid_b64, char **epk_out);

/* Result of ehem_notify_register_init(). */
typedef struct ehem_notify_register_info {
    char *rid;    /* registration id — poll register/check with it */
    char *link;   /* URL the phone app consumes (the QR payload's `link`) */
} ehem_notify_register_info;

/*
 * Start a registration at the broker: POST /register/init with the device's
 * `eid` and `request` (from ehem_ext_init) and the broker `epk` (from
 * ehem_notify_session). The returned `link` goes into the QR payload the
 * phone scans; composing/rendering that payload is the caller's (REQ-TOOL-016
 * puts `{link, hash, user, email, hostname}` behind a terminal QR).
 */
EHEM_API ehem_rc ehem_notify_register_init(ehem_ctx *ctx,
                                           const char *notify_url,
                                           const char *epk_b64,
                                           const char *eid_b64,
                                           const char *request_jwt,
                                           ehem_notify_register_info **out);

/* Release a register-init result. NULL is a no-op. */
EHEM_API void ehem_notify_register_info_free(ehem_notify_register_info *info);

/* Result of ehem_notify_register_check(). */
typedef struct ehem_notify_pairing_reply {
    int   pending;   /* nonzero: the phone has not completed its side (202) */
    char *pid;       /* set when !pending: forward to ehem_ext_validate */
    char *reply;     /* set when !pending: forward to ehem_ext_validate */
} ehem_notify_pairing_reply;

/*
 * Poll one registration: GET /register/check/<rid>. 202 → EHEM_OK with
 * `pending` set; 200 → EHEM_OK with the `{pid, reply}` the phone produced.
 */
EHEM_API ehem_rc ehem_notify_register_check(ehem_ctx *ctx,
                                            const char *notify_url,
                                            const char *rid,
                                            ehem_notify_pairing_reply **out);

/* Release a register-check result. NULL is a no-op. */
EHEM_API void ehem_notify_pairing_reply_free(ehem_notify_pairing_reply *r);

/*
 * Complete a registration at the broker: POST /register/finalise/<rid> with
 * the device's /ext/validate result passed through VERBATIM ({"kid","code"}
 * — the phone verifies `code` to learn the device accepted it).
 */
EHEM_API ehem_rc ehem_notify_register_finalise(ehem_ctx *ctx,
                                               const char *notify_url,
                                               const char *rid,
                                               const char *kid_hex,
                                               const char *code_b64);

/*
 * Push a confirmation request to every paired phone: POST /event/new with
 * the device's /ext/request result passed through VERBATIM ({"authreq",
 * "epk"}). On success *eventid_out names the event for polling (free with
 * ehem_notify_string_free). NEVER call this casually against a device whose
 * owner has a real phone paired — it rings it (REQ-TEST-006 gates live
 * tests accordingly).
 */
EHEM_API ehem_rc ehem_notify_event_new(ehem_ctx *ctx, const char *notify_url,
                                       const char *authreq_jwt,
                                       const char *epk_b64,
                                       char **eventid_out);

/* Result of ehem_notify_event_check(). Exactly one of the three states:
 * pending, denied, or approved (authreply set). */
typedef struct ehem_notify_event_result {
    int   pending;     /* 202: no answer yet */
    int   denied;      /* 200 with `deny` set: rejected on the phone — the
                        * app sends NO authreply on deny (tester T-6 note) */
    char *authreply;   /* 200 approved: forward to ehem_ext_token */
} ehem_notify_event_result;

/*
 * Poll one event: GET /event/check/<eventid>. 202 → pending; 200 with
 * `deny` → denied; 200 with `authreply` → approved. A 200 carrying neither
 * is EHEM_ERR_PROTOCOL (an unknown broker shape — recorded with the body).
 */
EHEM_API ehem_rc ehem_notify_event_check(ehem_ctx *ctx,
                                         const char *notify_url,
                                         const char *eventid,
                                         ehem_notify_event_result **out);

/* Release an event-check result. NULL is a no-op. */
EHEM_API void ehem_notify_event_result_free(ehem_notify_event_result *r);

/* Release a string returned by this family (session epk, event id).
 * NULL is a no-op. */
EHEM_API void ehem_notify_string_free(char *s);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_AUTH_H */
