/*
 * proto_auth.c — the session engine: passphrase login, the eJWT challenge
 * exchange, and a scope-keyed bearer-token cache with silent refresh.
 *
 * implements: REQ-AUTH-001, REQ-AUTH-002, REQ-AUTH-004, REQ-AUTH-005,
 *             REQ-AUTH-010
 *
 * Mirrors the reference python client's Auth (encedo-hem-python-api auth.py):
 * ehem_login() records the credential (lazily, no network); the first
 * authenticated call funnels through ehem_auth_ensure_token(), which returns a
 * cached token or runs GET /api/auth/token → derive → POST /api/auth/token.
 * All secret intermediates (KDF output, private scalar, shared secret, the
 * signed eJWT) are zeroized after use, and the retained passphrase is scrubbed
 * on logout / destroy (and after first use when retention is disabled).
 */
#include "ehem/auth.h"
#include "proto_auth.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crypto_shim.h"
#include "ejwt.h"
#include "json.h"
#include "proto_common.h"
#include "transport.h"

/* Login parameters pinned by REQ-AUTH-001 / REQ-AUTH-002. */
#define AUTH_PBKDF2_ITERS   600000        /* PBKDF2-HMAC-SHA256 rounds        */
#define AUTH_TOKEN_LIFETIME 3600          /* seconds; token lifetime we ask   */
#define AUTH_TOKEN_SKEW     60            /* treat as expired this early      */
#define AUTH_TOKEN_PATH     "/api/auth/token"

/* -------------------------------------------------------------------------- */
/* Session state                                                              */
/* -------------------------------------------------------------------------- */

/* One cached bearer token, keyed by its scope string. */
struct token_entry {
    char               *scope;   /* owned */
    char               *token;   /* owned, NUL-terminated */
    int64_t             exp;     /* cache expiry (unix), skew already applied */
    struct token_entry *next;
};

struct ehem_auth {
    char   *passphrase;       /* retained copy (NUL-terminated), or NULL */
    size_t  passphrase_len;   /* length excluding the NUL */
    bool    retain;           /* false → scrub passphrase after first use */
    char   *username;         /* last challenge `lbl`, or NULL (debug only) */
    bool    mobile;           /* REQ-AUTH-010: acquire via push-confirm */
    struct token_entry *cache;
};

/* -------------------------------------------------------------------------- */
/* Clock seam (test-overridable)                                              */
/* -------------------------------------------------------------------------- */

static int64_t (*g_now_fn)(void) = NULL;

static int64_t auth_now(void)
{
    return (g_now_fn != NULL) ? g_now_fn() : (int64_t)time(NULL);
}

void ehem_auth_test_set_clock(int64_t (*fn)(void))
{
    g_now_fn = fn;
}

/* PBKDF2 iteration count, test-overridable. 0 → the pinned production value. */
static uint32_t g_kdf_iters = 0;

static uint32_t auth_kdf_iters(void)
{
    return (g_kdf_iters != 0) ? g_kdf_iters : (uint32_t)AUTH_PBKDF2_ITERS;
}

void ehem_auth_test_set_kdf_iters(uint32_t iters)
{
    g_kdf_iters = iters;
}

/* -------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* -------------------------------------------------------------------------- */

static char *auth_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* Scrub + free the retained passphrase (idempotent). */
static void scrub_passphrase(struct ehem_auth *a)
{
    if (a->passphrase != NULL) {
        ehem_zeroize(a->passphrase, a->passphrase_len);
        free(a->passphrase);
        a->passphrase = NULL;
    }
    a->passphrase_len = 0;
}

/* -------------------------------------------------------------------------- */
/* Token cache                                                                */
/* -------------------------------------------------------------------------- */

static struct token_entry *cache_find(struct ehem_auth *a, const char *scope)
{
    struct token_entry *e;
    for (e = a->cache; e != NULL; e = e->next) {
        if (strcmp(e->scope, scope) == 0) {
            return e;
        }
    }
    return NULL;
}

static void cache_clear(struct ehem_auth *a)
{
    struct token_entry *e = a->cache;
    while (e != NULL) {
        struct token_entry *next = e->next;
        free(e->scope);
        /* A bearer token is sensitive material — scrub before releasing. */
        if (e->token != NULL) {
            ehem_zeroize(e->token, strlen(e->token));
            free(e->token);
        }
        free(e);
        e = next;
    }
    a->cache = NULL;
}

/* Insert or replace the cached token for `scope`. Returns the stored entry, or
 * NULL on allocation failure (the cache is left unchanged on failure). */
static struct token_entry *cache_put(struct ehem_auth *a, const char *scope,
                                     const char *token, int64_t exp)
{
    struct token_entry *e = cache_find(a, scope);
    char *tok_copy = auth_strdup(token);
    if (tok_copy == NULL) {
        return NULL;
    }
    if (e != NULL) {
        /* Replace the token in place; scrub the old one first. */
        ehem_zeroize(e->token, strlen(e->token));
        free(e->token);
        e->token = tok_copy;
        e->exp   = exp;
        return e;
    }
    e = calloc(1, sizeof *e);
    if (e == NULL) {
        ehem_zeroize(tok_copy, strlen(tok_copy));
        free(tok_copy);
        return NULL;
    }
    e->scope = auth_strdup(scope);
    if (e->scope == NULL) {
        ehem_zeroize(tok_copy, strlen(tok_copy));
        free(tok_copy);
        free(e);
        return NULL;
    }
    e->token = tok_copy;
    e->exp   = exp;
    e->next  = a->cache;
    a->cache = e;
    return e;
}

/* Read the `exp` claim (seconds) from a bearer JWT's payload segment. Best
 * effort: a missing/malformed value returns false, and the caller falls back to
 * the requested lifetime (matches the python client's _decode_bearer_exp). */
static bool decode_bearer_exp(const char *token, int64_t *out)
{
    const char *d1, *d2;
    uint8_t raw[1024];
    size_t seg_len, n;
    ehem_json *payload;
    bool ok;

    d1 = strchr(token, '.');
    if (d1 == NULL) {
        return false;
    }
    d2 = strchr(d1 + 1, '.');
    if (d2 == NULL) {
        return false;
    }
    seg_len = (size_t)(d2 - (d1 + 1));
    if (seg_len == 0 || seg_len > sizeof raw) {
        return false;
    }
    n = ehem_b64url_decode(d1 + 1, seg_len, raw, sizeof raw);
    if (n == (size_t)-1) {
        return false;
    }
    payload = ehem_json_parse((const char *)raw, n);
    if (payload == NULL) {
        return false;
    }
    ok = ehem_json_get_int64(payload, "exp", out);
    ehem_json_free(payload);
    return ok;
}

/*
 * Seed the cache with an externally-acquired bearer (the ExtAuth confirm
 * engine, REQ-AUTH-009): the mobile flow mints its bearer via
 * /api/auth/ext/token rather than the passphrase exchange, but the cache —
 * and therefore every authenticated binding — treats it identically. Creates
 * the auth state if the context has none (mobile mode holds no passphrase).
 * Entry expiry honors the bearer's own exp claim minus skew, with the same
 * fallback as the login path.
 */
ehem_rc ehem_auth_cache_seed(ehem_ctx *ctx, const char *scope,
                             const char *token)
{
    struct ehem_auth *a;
    int64_t real_exp;

    if (ctx == NULL || scope == NULL || token == NULL) {
        return EHEM_ERR_ARG;
    }
    if (ctx->auth == NULL) {
        ctx->auth = calloc(1, sizeof *ctx->auth);
        if (ctx->auth == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL,
                                 "out of memory");
        }
    }
    a = ctx->auth;
    if (!decode_bearer_exp(token, &real_exp)) {
        real_exp = auth_now() + AUTH_TOKEN_LIFETIME;
    }
    if (cache_put(a, scope, token, real_exp - AUTH_TOKEN_SKEW) == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    return EHEM_OK;
}

/* Serialize the login POST body: {"auth":"<ejwt>"}. Returns a cJSON-allocated
 * string (free with ehem_json_string_free), or NULL on allocation failure. */
static char *build_auth_body(const char *ejwt)
{
    ehem_json *obj = ehem_json_new_object();
    char *body;
    if (obj == NULL) {
        return NULL;
    }
    if (!ehem_json_add_string(obj, "auth", ejwt)) {
        ehem_json_free(obj);
        return NULL;
    }
    body = ehem_json_print(obj);
    ehem_json_free(obj);
    return body;
}

/* -------------------------------------------------------------------------- */
/* Challenge fetch (with RTC-unset recovery)                                  */
/* -------------------------------------------------------------------------- */

/*
 * GET the login challenge. A 403 means the device RTC is unset — it cannot
 * issue time-bounded tokens yet. Reuse the check-in machinery (REQ-SYS-003) to
 * set the clock, then retry the challenge once, unless auto check-in is
 * disabled, we are already inside a check-in (recursion guard), or this
 * ensure-token call has already spent its one recovery check-in
 * (*recovery_spent — shared with the REQ-AUTH-004 drift recovery so the two
 * paths compose instead of multiplying) — mirrors the REQ-NET-005 expired-cert
 * recovery, keyed on 403 instead of a TLS failure.
 */
static ehem_rc fetch_challenge(ehem_ctx *ctx, bool *recovery_spent,
                               ehem_json **out)
{
    /* The login exchange is itself unauthenticated (scope NULL) — it is how a
     * bearer is obtained in the first place. */
    ehem_rc rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, AUTH_TOKEN_PATH,
                                         NULL, NULL, EHEM_TLS_REQ_DEFAULT, out);
    if (rc == EHEM_OK) {
        return EHEM_OK;
    }
    if (ehem_last_error(ctx)->http_status == 403 &&
        !*recovery_spent && !ctx->no_auto_checkin && !ctx->in_checkin) {
        *recovery_spent = true;
        /* Device legs run relaxed: an RTC-broken device's own view of its
         * certificate validity is unreliable, and the check-in payloads are
         * cloud-signed (same posture as the explicit ehem_system_checkin). */
        ehem_rc crc = ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL);
        if (crc != EHEM_OK) {
            /* Copy the check-in's message out before ehem_ctx_fail overwrites
             * the shared last-error buffer it lives in (no aliasing). */
            char checkin_detail[EHEM_ERR_MSG_MAX];
            snprintf(checkin_detail, sizeof checkin_detail, "%s",
                     ehem_last_error(ctx)->message);
            return ehem_ctx_fail(ctx, rc, 403, NULL,
                                 AUTH_TOKEN_PATH ": HTTP 403 (device RTC not "
                                 "set; automatic check-in failed: %s)",
                                 checkin_detail);
        }
        rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, AUTH_TOKEN_PATH,
                                     NULL, NULL, EHEM_TLS_REQ_DEFAULT, out);
    }
    return rc;
}

/*
 * Clock-drift evidence (REQ-AUTH-004). The challenge `exp` is the device-side
 * submit deadline, device_now + 60 s (firmware gen_auth_token) — the one place
 * the device's clock shows through the login flow. When |device_now − local
 * now| exceeds the eJWT skew margin, a 401 on the token POST is treated as
 * drift-induced (the device RTC runs ~8% fast; past the requested TTL every
 * login 401s because the requested exp is already past device-side) rather
 * than as a bad credential. A missing/unreadable exp yields no evidence.
 */
#define AUTH_CHALLENGE_DEADLINE 60   /* challenge exp = device_now + this */

static bool challenge_drift_evident(const ehem_json *challenge, int64_t now)
{
    int64_t exp, drift;
    if (!ehem_json_get_int64(challenge, "exp", &exp)) {
        return false;
    }
    drift = (exp - AUTH_CHALLENGE_DEADLINE) - now;
    return drift > AUTH_TOKEN_SKEW || drift < -AUTH_TOKEN_SKEW;
}

/* -------------------------------------------------------------------------- */
/* Token acquisition (the REQ-AUTH-001 flow)                                  */
/* -------------------------------------------------------------------------- */

static ehem_rc acquire_token(ehem_ctx *ctx, struct ehem_auth *a,
                             const char *scope, int64_t now,
                             bool *recovery_spent, bool *drift_evident,
                             const char **token_out)
{
    ehem_json *challenge = NULL;
    ehem_json *result = NULL;
    const char *eid, *spk, *jti, *lbl, *token;
    int64_t real_exp;
    uint8_t peer_pub[EHEM_X25519_KEYSIZE];
    uint8_t seed[EHEM_X25519_KEYSIZE];
    uint8_t priv[EHEM_X25519_KEYSIZE];
    uint8_t user_pub[EHEM_X25519_KEYSIZE];
    uint8_t shared[EHEM_X25519_KEYSIZE];
    char *ejwt = NULL;
    char *body = NULL;
    struct token_entry *entry;
    ehem_rc rc;

    /* The derivation below needs wolfCrypt's process-global init (the RNG
     * behind X25519 blinding). The default transport factory already runs
     * ehem_global_init(), but a caller-supplied transport reaches this point
     * without it; idempotent, so effectively free after the first call. */
    rc = ehem_global_init();
    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, rc, 0, NULL,
                             AUTH_TOKEN_PATH ": process-global init failed");
    }

    rc = fetch_challenge(ctx, recovery_spent, &challenge);
    if (rc != EHEM_OK) {
        return rc;   /* last-error already recorded by the request path */
    }

    /* The challenge `exp` is read ONLY as drift evidence for the REQ-AUTH-004
     * recovery decision — it is the response deadline (enforced server-side by
     * the `jti` nonce), never a token-lifetime input (STEP-M2-045). */
    *drift_evident = challenge_drift_evident(challenge, now);

    /* Required challenge fields. */
    if (!ehem_json_get_string(challenge, "eid", &eid) ||
        !ehem_json_get_string(challenge, "spk", &spk) ||
        !ehem_json_get_string(challenge, "jti", &jti)) {
        ehem_json_free(challenge);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             AUTH_TOKEN_PATH ": malformed challenge "
                             "(need eid, spk, jti)");
    }
    /* Optional username, kept for debugging / last-error context. */
    if (ehem_json_get_string(challenge, "lbl", &lbl)) {
        char *u = auth_strdup(lbl);
        if (u != NULL) {
            free(a->username);
            a->username = u;
        }
    }

    /* spk is the device session public key in standard base64 → 32 raw bytes. */
    if (ehem_b64_std_decode(spk, strlen(spk), peer_pub, sizeof peer_pub)
            != EHEM_X25519_KEYSIZE) {
        ehem_json_free(challenge);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             AUTH_TOKEN_PATH ": challenge spk is not a "
                             "32-byte X25519 key");
    }

    /* Derive: PBKDF2(passphrase, salt=eid) → seed → X25519 keypair → ECDH. */
    rc = ehem_kdf_pbkdf2_sha256((const uint8_t *)a->passphrase, a->passphrase_len,
                                (const uint8_t *)eid, strlen(eid),
                                auth_kdf_iters(), seed, sizeof seed);
    if (rc == EHEM_OK) {
        rc = ehem_x25519_keypair_from_seed(seed, priv, user_pub);
    }
    if (rc == EHEM_OK) {
        rc = ehem_x25519_shared(priv, peer_pub, shared);
    }
    if (rc == EHEM_OK) {
        rc = ehem_ejwt_build(jti, spk, scope, user_pub, shared,
                             now, now + AUTH_TOKEN_LIFETIME, &ejwt);
    }
    /* Secret intermediates are done with — scrub regardless of success. */
    ehem_zeroize(seed, sizeof seed);
    ehem_zeroize(priv, sizeof priv);
    ehem_zeroize(shared, sizeof shared);
    ehem_json_free(challenge);
    challenge = NULL;
    if (rc != EHEM_OK) {
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             AUTH_TOKEN_PATH ": credential derivation failed");
    }

    body = build_auth_body(ejwt);
    /* The signed eJWT is a single-use credential — scrub the copies. */
    ehem_zeroize(ejwt, strlen(ejwt));
    ehem_ejwt_free(ejwt);
    if (body == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    rc = ehem_proto_request_json(ctx, EHEM_HTTP_POST, AUTH_TOKEN_PATH,
                                 body, NULL, EHEM_TLS_REQ_DEFAULT, &result);
    ehem_zeroize(body, strlen(body));
    ehem_json_string_free(body);
    if (rc != EHEM_OK) {
        /* POST 401 → EHEM_ERR_AUTH_FAILED with the device payload already in
         * last-error (proto_common mapping); pass it through. */
        return rc;
    }

    if (!ehem_json_get_string(result, "token", &token)) {
        ehem_json_free(result);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 200, NULL,
                             AUTH_TOKEN_PATH ": response has no 'token'");
    }

    /* Cache the token honoring its own exp claim when readable (the device
     * shortens some scopes), else the requested lifetime; minus the skew. */
    if (!decode_bearer_exp(token, &real_exp)) {
        real_exp = now + AUTH_TOKEN_LIFETIME;
    }
    entry = cache_put(a, scope, token, real_exp - AUTH_TOKEN_SKEW);
    ehem_json_free(result);
    if (entry == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }

    /* Retention opt-out: the passphrase has served its purpose — scrub it, so a
     * later miss/expiry fails AUTH_EXPIRED rather than re-deriving. */
    if (!a->retain) {
        scrub_passphrase(a);
    }

    *token_out = entry->token;
    return EHEM_OK;
}

/* -------------------------------------------------------------------------- */
/* ensure-token chokepoint                                                    */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_auth_ensure_token(ehem_ctx *ctx, const char *scope,
                               const char **token_out)
{
    struct ehem_auth *a;
    struct token_entry *e;
    int64_t now;
    bool recovery_spent = false;   /* one recovery check-in per call, total */
    bool drift_evident  = false;
    bool proactive_failed = false;
    ehem_rc rc;

    if (ctx == NULL || scope == NULL || token_out == NULL) {
        return EHEM_ERR_ARG;
    }
    *token_out = NULL;
    a = ctx->auth;
    now = auth_now();

    /* Fresh cache hit → no network. */
    if (a != NULL) {
        e = cache_find(a, scope);
        if (e != NULL && e->exp > now) {
            *token_out = e->token;
            return EHEM_OK;
        }
    }

    /*
     * Mobile mode (REQ-AUTH-010): a miss acquires via the push-confirm
     * engine — the user answers on their phone within confirm_timeout_ms and
     * the minted bearer lands in this same cache; rejection/timeout surface
     * from whatever binding triggered the acquisition. The proactive
     * session-start check-in applies here exactly as it does below (the
     * ext endpoints need the RTC set just like the challenge GET).
     */
    if (a != NULL && a->mobile) {
        ehem_ext_confirm *confirm = NULL;

        if (ctx->checkin_on_login && !ctx->checkin_on_login_done &&
            !ctx->in_checkin) {
            ctx->checkin_on_login_done = true;
            proactive_failed =
                (ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL)
                 != EHEM_OK);
            (void)proactive_failed;   /* breadcrumb below is passphrase-path */
        }

        rc = ehem_ext_confirm_begin(ctx, NULL, scope, NULL, NULL, &confirm);
        if (rc != EHEM_OK) {
            return rc;
        }
        rc = ehem_ext_confirm_wait(ctx, confirm, ctx->confirm_timeout_ms);
        ehem_ext_confirm_cancel(confirm);
        if (rc != EHEM_OK) {
            return rc;   /* USER_REJECTED / CONFIRM_TIMEOUT / broker error */
        }
        e = cache_find(a, scope);
        if (e == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                                 "mobile login: confirmation succeeded but "
                                 "no bearer was cached for scope '%s'", scope);
        }
        *token_out = e->token;
        return EHEM_OK;
    }

    /* Need to acquire; only possible while a credential is retained. */
    if (a == NULL || a->passphrase == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_AUTH_EXPIRED, 0, NULL,
                             "no credential retained for scope '%s' — call "
                             "ehem_login()", scope);
    }

    /* Proactive session-start check-in (REQ-AUTH-005): once per context,
     * before the first acquisition, best-effort — a failure never blocks the
     * login (and the REQ-AUTH-004 recovery below stays the backstop). The
     * `done` latch is set before the attempt so even a failing check-in is
     * never repeated. */
    if (ctx->checkin_on_login && !ctx->checkin_on_login_done &&
        !ctx->in_checkin) {
        ctx->checkin_on_login_done = true;
        proactive_failed = (ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL)
                            != EHEM_OK);
    }

    rc = acquire_token(ctx, a, scope, now, &recovery_spent, &drift_evident,
                       token_out);

    /* Clock-drift login recovery (REQ-AUTH-004): a 401 on the token POST with
     * measured drift in the challenge is the device rejecting our requested
     * exp as already past, not a bad credential — run ONE check-in (the
     * firmware resynchronizes its RTC as a side effect) and retry the whole
     * flow once (the jti nonce is single-use, so the retry re-fetches a fresh
     * challenge). Without drift evidence a 401 stays AUTH_FAILED immediately:
     * a wrong passphrase must not cost a check-in round-trip. Shares the
     * one-recovery budget with the challenge-403 path via recovery_spent. */
    if (rc == EHEM_ERR_AUTH_FAILED && drift_evident &&
        ehem_last_error(ctx)->http_status == 401 &&
        !recovery_spent && !ctx->no_auto_checkin && !ctx->in_checkin) {
        recovery_spent = true;
        if (ehem_checkin_run(ctx, /*relax_device_tls=*/1, NULL) == EHEM_OK) {
            now = auth_now();
            rc = acquire_token(ctx, a, scope, now, &recovery_spent,
                               &drift_evident, token_out);
        } else {
            /* Keep the login failure as the outcome; note the failed rescue. */
            char checkin_detail[EHEM_ERR_MSG_MAX];
            snprintf(checkin_detail, sizeof checkin_detail, "%s",
                     ehem_last_error(ctx)->message);
            return ehem_ctx_fail(ctx, EHEM_ERR_AUTH_FAILED, 401, NULL,
                                 AUTH_TOKEN_PATH ": HTTP 401 with device clock "
                                 "drift; recovery check-in failed: %s",
                                 checkin_detail);
        }
    }

    /* A successful login after a failed proactive check-in: leave a breadcrumb
     * in the (otherwise idle) last-error message so the failure is observable
     * without failing anything. rc/http_status stay untouched. */
    if (rc == EHEM_OK && proactive_failed) {
        snprintf(ctx->err_message, sizeof ctx->err_message,
                 "checkin_on_login: best-effort check-in failed; "
                 "login proceeded");
    }
    return rc;
}

void ehem_auth_invalidate(ehem_ctx *ctx, const char *scope)
{
    struct ehem_auth *a;
    struct token_entry *e, *prev;

    if (ctx == NULL || ctx->auth == NULL) {
        return;
    }
    a = ctx->auth;
    if (scope == NULL) {
        cache_clear(a);
        return;
    }
    prev = NULL;
    for (e = a->cache; e != NULL; prev = e, e = e->next) {
        if (strcmp(e->scope, scope) == 0) {
            if (prev == NULL) {
                a->cache = e->next;
            } else {
                prev->next = e->next;
            }
            free(e->scope);
            ehem_zeroize(e->token, strlen(e->token));
            free(e->token);
            free(e);
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Session lifecycle                                                          */
/* -------------------------------------------------------------------------- */

void ehem_auth_destroy(struct ehem_auth *a)
{
    if (a == NULL) {
        return;
    }
    scrub_passphrase(a);
    free(a->username);
    cache_clear(a);
    free(a);
}

ehem_rc ehem_login(ehem_ctx *ctx, const char *passphrase)
{
    struct ehem_auth *a;
    char *copy;
    size_t n;

    if (ctx == NULL || passphrase == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    /* Copy the passphrase first, so a failure leaves any prior session intact. */
    n = strlen(passphrase);
    copy = malloc(n + 1);
    if (copy == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
    }
    memcpy(copy, passphrase, n + 1);

    if (ctx->auth == NULL) {
        ctx->auth = calloc(1, sizeof *ctx->auth);
        if (ctx->auth == NULL) {
            ehem_zeroize(copy, n);
            free(copy);
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL, "out of memory");
        }
    }
    a = ctx->auth;

    /* Re-login: drop any cached tokens and replace the stored credential. */
    cache_clear(a);
    free(a->username);
    a->username = NULL;
    scrub_passphrase(a);

    a->passphrase     = copy;
    a->passphrase_len = n;
    a->retain         = !ctx->no_credential_retention;
    a->mobile         = false;   /* last login call wins (REQ-AUTH-010) */
    return EHEM_OK;
}

/*
 * Mobile login mode (REQ-AUTH-010): same lazy contract as ehem_login, but
 * the recorded "credential" is the MODE — ensure_token acquires bearers via
 * the push-confirm engine instead of the passphrase exchange. Mutually
 * exclusive with a passphrase session: switching scrubs the loser.
 */
ehem_rc ehem_login_mobile(ehem_ctx *ctx)
{
    struct ehem_auth *a;

    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);

    if (ctx->auth == NULL) {
        ctx->auth = calloc(1, sizeof *ctx->auth);
        if (ctx->auth == NULL) {
            return ehem_ctx_fail(ctx, EHEM_ERR_NOMEM, 0, NULL,
                                 "out of memory");
        }
    }
    a = ctx->auth;

    /* Re-login: drop cached tokens and any retained passphrase. */
    cache_clear(a);
    free(a->username);
    a->username = NULL;
    scrub_passphrase(a);

    a->mobile = true;
    return EHEM_OK;
}

/* Internal (proto_auth.h): mode query for bindings that must fail fast in
 * mobile mode (the pairing trio demands sub=="U", REQ-AUTH-010). */
bool ehem_auth_is_mobile(const ehem_ctx *ctx)
{
    return ctx != NULL && ctx->auth != NULL && ctx->auth->mobile;
}

ehem_rc ehem_logout(ehem_ctx *ctx)
{
    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);
    ehem_auth_destroy(ctx->auth);
    ctx->auth = NULL;
    return EHEM_OK;
}
