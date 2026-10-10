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
 * Session lifecycle: ehem_login() (passphrase) or ehem_login_mobile()
 * (push-confirm, REQ-AUTH-010) establish a session; ehem_logout() ends it.
 * Token acquisition itself is lazy and automatic — the authenticated
 * protocol bindings acquire and cache a token for the scope they need on
 * first use, and silently refresh it before it expires. There is no public
 * "get token" call for the session engine; the one deliberate exception is
 * ehem_ext_token(), where returning the minted bearer to the broker-driving
 * caller is the point.
 */
#ifndef EHEM_AUTH_H
#define EHEM_AUTH_H

#include <stdbool.h>
#include <stdint.h>

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

/*
 * Mobile login mode (REQ-AUTH-010): establish a session whose bearers are
 * acquired by PUSH CONFIRMATION on a paired phone instead of a passphrase.
 * Same lazy contract as ehem_login() — no network here. Afterwards, any
 * binding needing a token for a scope not in the cache fires one push
 * (ehem_ext_confirm machinery, all legs credential-free) and blocks up to
 * ehem_options.confirm_timeout_ms (0 → 60 s): the user's answer surfaces
 * from that binding call as success, EHEM_ERR_USER_REJECTED, or
 * EHEM_ERR_CONFIRM_TIMEOUT — the pinpad-reader UX.
 *
 * Consequences to know:
 *   - one push per SCOPE on first use; per-KID scopes (keymgmt:use:<kid>)
 *     mean one push per key and a 15-minute bearer (firmware rewrite) — a
 *     consumer wanting fewer pushes requests broader scopes;
 *   - the minted bearers carry the authenticator kid as `sub`, so the
 *     pairing endpoints (which demand sub="U") reject them — pairing
 *     management always needs a passphrase session; the SDK fails those
 *     calls fast in mobile mode without firing a push;
 *   - mutually exclusive with ehem_login(): the last call wins and the
 *     loser's credential material is scrubbed. ehem_logout() ends either
 *     kind of session.
 */
EHEM_API ehem_rc ehem_login_mobile(ehem_ctx *ctx);

/* ==========================================================================
 * Device initialisation (REQ-AUTH-011) and the Manager-compatible master
 * secret (REQ-AUTH-012). Both are verified ATTENDED-ONLY (ARCHITECTURE.md §9):
 * no live test exists for them; hem-tool init-device drives them by hand.
 * implements: REQ-AUTH-011, REQ-AUTH-012
 * ========================================================================== */

#define EHEM_MASTER_SECRET_SIZE 32

/*
 * Generate a fresh 24-word BIP39 English mnemonic (256 bits of entropy from
 * the crypto backend's DRBG) — the master persona the way Encedo Manager
 * creates one at init. *words_out is a library-allocated, NUL-terminated string
 * of lowercase words joined by single spaces; release it with
 * ehem_mnemonic_free() (which scrubs it). The device offers NO way to rotate
 * the master key later, so the caller must keep these words. `ctx` may be NULL
 * (then no error detail is recorded). EHEM_ERR_ARG on a NULL words_out.
 */
EHEM_API ehem_rc ehem_mnemonic_generate(ehem_ctx *ctx, char **words_out);

/* Scrub and free a mnemonic from ehem_mnemonic_generate(). NULL is a no-op. */
EHEM_API void ehem_mnemonic_free(char *words);

/*
 * Derive the 32-byte master secret from a 24-word BIP39 English mnemonic
 * EXACTLY as Encedo Manager does (assets/build.js initFinal and the
 * master-passphrase prompt, assets/jsbip39_v1.js): the words are validated
 * (any whitespace between them, lowercase words from the English list, BIP39
 * checksum), the standard seed is PBKDF2-HMAC-SHA512(mnemonic, "mnemonic",
 * 2048 rounds, 64 bytes), and the secret is the Manager's `substr(1, 64)`
 * slice of that seed's hex — a nibble-shifted 32 bytes. Feed the result to
 * ehem_init_params.master_secret; it is also the master persona of any device
 * the Manager initialised from the same words. An invalid mnemonic returns
 * EHEM_ERR_ARG with the reason in ehem_last_error(ctx) (when ctx is non-NULL)
 * and writes nothing. Intermediates are zeroized; the caller owns the secret.
 */
EHEM_API ehem_rc ehem_master_secret_from_mnemonic(ehem_ctx *ctx, const char *words,
                                                  uint8_t secret[EHEM_MASTER_SECRET_SIZE]);

/*
 * Inputs for ehem_device_init(). Initialise with ehem_init_params_init() (it
 * stamps abi_size and zeroes every field — zero/NULL means "the Manager's
 * default", so the struct can grow append-only like ehem_options), then set:
 *
 *   passphrase          REQUIRED — the user persona: becomes the device's user
 *                       password (cfg.userkey = X25519 public key of the
 *                       REQ-AUTH-001 derivation, so ehem_login() with the same
 *                       passphrase works afterwards);
 *   master_secret       REQUIRED — EHEM_MASTER_SECRET_SIZE bytes, the master
 *                       persona (cfg.masterkey); signs the init JWT. From
 *                       ehem_master_secret_from_mnemonic() for Manager
 *                       compatibility, or any 32 bytes the caller keeps;
 *   user, email,        REQUIRED — cfg.user (device user identity), cfg.email,
 *   hostname, ip        cfg.hostname, cfg.ip ("A.B.C.D/prefix");
 *   storage_mode        REQUIRED (> 0) — cfg.storage_mode;
 *   storage_disk0size   REQUIRED (> 0) — cfg.storage_disk0size, bytes;
 *   origin              cfg.origin (CORS); NULL → "*" (Manager default);
 *   dnsd                nonzero → cfg.dnsd true; default false;
 *   no_trusted_ts,      nonzero → the corresponding cfg boolean FALSE; zero
 *   no_trusted_backend, keeps the Manager's default of true;
 *   no_allow_keysearch
 *   gen_csr             nonzero → cfg.gen_csr true (the device generates a TLS
 *                       CSR, returned in ehem_init_info.csr); default false;
 *   ctx_id              cfg.ctx; default 0.
 *
 * The 13 fields the firmware's completeness mask demands are always sent.
 */
typedef struct ehem_init_params {
    size_t         abi_size;            /* set by ehem_init_params_init() */
    const char    *passphrase;
    const uint8_t *master_secret;       /* EHEM_MASTER_SECRET_SIZE bytes */
    const char    *user;
    const char    *email;
    const char    *hostname;
    const char    *ip;
    int            storage_mode;
    int64_t        storage_disk0size;
    const char    *origin;
    int            dnsd;
    int            no_trusted_ts;
    int            no_trusted_backend;
    int            no_allow_keysearch;
    int            gen_csr;
    int            ctx_id;
} ehem_init_params;

/* Zero the params and stamp abi_size. Call before setting fields. */
EHEM_API void ehem_init_params_init(ehem_init_params *params);

/* What the device returned from a successful init. Free with
 * ehem_init_info_free(). The bearer the device also returns (sub "U", scope
 * "system:config") is not exposed: it is placed in the context's token cache. */
typedef struct ehem_init_info {
    bool  reboot_required;   /* hostname/IP/storage changed from the defaults */
    char *instanceid;        /* the new device UUID (always present) */
    char *csr;               /* PEM TLS CSR when gen_csr succeeded, else NULL */
    char *genuine;           /* fresh attestation token, NULL if absent */
} ehem_init_info;

/*
 * Personalise an UNINITIALISED device — POST /api/auth/init — the way Encedo
 * Manager's initFinal does it (REQ-AUTH-011):
 *   1. GET /api/auth/init (no auth) → challenge {exp, spk, jti, genuine, eid};
 *   2. user key: PBKDF2-HMAC-SHA256(passphrase, salt = eid, 600 000, 32 B) →
 *      X25519 keypair (REQ-AUTH-001); master key: X25519 keypair from
 *      master_secret;
 *   3. init JWT, SIGNED BY THE MASTER KEY: header {"ecdh":"x25519"}, claims
 *      {jti, aud: spk, exp: the challenge's exp, iat, iss: master public key,
 *      cfg: {...}}, HMAC-SHA256 keyed with ECDH(master, spk);
 *   4. POST {"init": "<jwt>"} → {reboot_required, instanceid, token, csr,
 *      genuine}.
 * On success the context holds a passphrase session for the new user persona
 * (as if ehem_login(passphrase) had been called, same retention rule) with the
 * returned "system:config" bearer already cached — a following config write
 * (e.g. ehem_system_config_install_cert) needs no login round-trip.
 *
 * Preconditions the device enforces, in order — each is reported with a
 * plain-language ehem_last_error() detail: RTC set (else HTTP 403 →
 * EHEM_ERR_DEVICE: run ehem_system_checkin() first — this call does NOT
 * check in by itself), not already initialised (406 → EHEM_ERR_DEVICE: wipe it
 * first, ehem_system_wipeout), self-test state 0 (409 → EHEM_ERR_DEVICE). The
 * POST adds 401 (JWT/jti rejected → EHEM_ERR_AUTH_FAILED) and 400 (a cfg field
 * failed validation → EHEM_ERR_DEVICE, nothing written). A wiped device has no
 * TLS material: create the context with its http:// URL, then restore HTTPS
 * with ehem_tls_recover() after the init (and a reboot if reboot_required).
 * masterkey cannot be rotated later — only a wipe + re-init changes it. Secret
 * intermediates are zeroized; the master secret is never retained. `out` may be
 * NULL when the reply details are not needed.
 */
EHEM_API ehem_rc ehem_device_init(ehem_ctx *ctx, const ehem_init_params *params,
                                  ehem_init_info **out);

/* Release an init result. NULL is a no-op. */
EHEM_API void ehem_init_info_free(ehem_init_info *info);

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

/* --------------------------------------------------------------------------
 * Mobile confirmation engine (REQ-AUTH-009) — one push-confirm acquisition
 * as a pollable state machine, with a blocking wrapper on top.
 *
 *   begin  fires the push: broker session (credential-free GET) →
 *          /ext/request → broker event/new. Three network legs, no waiting.
 *   poll   ONE broker event/check. Never sleeps, applies no deadline —
 *          cadence and deadlines belong to the caller or to wait().
 *          Terminal outcomes: approved (the authreply is redeemed via
 *          /ext/token and the bearer lands in the ordinary scope-keyed
 *          token cache, so the next binding call for `scope` just works) —
 *          or EHEM_ERR_USER_REJECTED (denied on the phone; no token call).
 *          Transport/broker errors are returned but are NOT terminal — a
 *          flaky poll may simply be retried.
 *   wait   poll at a bounded interval (default 5 s) until terminal or
 *          `timeout_ms` elapses → EHEM_ERR_CONFIRM_TIMEOUT. A timeout does
 *          not invalidate the handle: the caller may resume waiting or
 *          cancel. A push answered on the phone after the caller gave up
 *          has no effect — /ext/token is never called.
 *   cancel frees the handle; no network (the broker event simply expires
 *          with the authreq). Safe in every state; the handle is
 *          single-use.
 *
 * The whole flow is credential-free by construction — this is HOW a context
 * with no passphrase obtains bearers (see ehem_login_mobile, REQ-AUTH-010).
 * -------------------------------------------------------------------------- */

/* An in-progress confirmation (opaque). */
typedef struct ehem_ext_confirm ehem_ext_confirm;

typedef enum ehem_confirm_status {
    EHEM_CONFIRM_PENDING = 0,   /* no answer from the phone yet */
    EHEM_CONFIRM_APPROVED       /* approved; bearer acquired and cached */
} ehem_confirm_status;

/*
 * Fire a push asking every paired authenticator to approve `scope`.
 * `notify_url` NULL → EHEM_DEFAULT_NOTIFY_URL; `ctx_str`/`note` as in
 * ehem_ext_request (the phone shows `note`; `ctx_str` is echoed into the
 * token). On success *out is the in-progress handle. Remember the firmware
 * scope rewrite: a `keymgmt:use:<kid>` scope shows the key's label on the
 * phone and yields a 15-minute bearer.
 */
EHEM_API ehem_rc ehem_ext_confirm_begin(ehem_ctx *ctx, const char *notify_url,
                                        const char *scope,
                                        const char *ctx_str, const char *note,
                                        ehem_ext_confirm **out);

/*
 * One poll. EHEM_OK + *status_out = PENDING (ask again later) or APPROVED
 * (bearer cached under the begin() scope — terminal). EHEM_ERR_USER_REJECTED
 * = denied on the phone (terminal). Other errors: the poll failed but the
 * confirmation is still live — retry or cancel. Polling a terminal handle is
 * EHEM_ERR_ARG.
 */
EHEM_API ehem_rc ehem_ext_confirm_poll(ehem_ctx *ctx, ehem_ext_confirm *c,
                                       ehem_confirm_status *status_out);

/*
 * Block until the confirmation resolves or `timeout_ms` (> 0) elapses:
 * EHEM_OK (approved + cached), EHEM_ERR_USER_REJECTED, or
 * EHEM_ERR_CONFIRM_TIMEOUT (handle stays valid). A timeout shorter than one
 * poll interval still polls at least once. Any other error aborts the wait
 * (the handle stays valid for a retry).
 */
EHEM_API ehem_rc ehem_ext_confirm_wait(ehem_ctx *ctx, ehem_ext_confirm *c,
                                       long timeout_ms);

/* Free a confirmation handle in any state. NULL is a no-op. */
EHEM_API void ehem_ext_confirm_cancel(ehem_ext_confirm *c);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_AUTH_H */
