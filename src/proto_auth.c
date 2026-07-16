/*
 * proto_auth.c — the session engine: passphrase login, the eJWT challenge
 * exchange, and a scope-keyed bearer-token cache with silent refresh.
 *
 * implements: REQ-AUTH-001, REQ-AUTH-002
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
 * disabled or we are already inside a check-in (recursion guard) — mirrors the
 * REQ-NET-005 expired-cert recovery, keyed on 403 instead of a TLS failure.
 */
static ehem_rc fetch_challenge(ehem_ctx *ctx, ehem_json **out)
{
    /* The login exchange is itself unauthenticated (scope NULL) — it is how a
     * bearer is obtained in the first place. */
    ehem_rc rc = ehem_proto_request_json(ctx, EHEM_HTTP_GET, AUTH_TOKEN_PATH,
                                         NULL, NULL, EHEM_TLS_REQ_DEFAULT, out);
    if (rc == EHEM_OK) {
        return EHEM_OK;
    }
    if (ehem_last_error(ctx)->http_status == 403 &&
        !ctx->no_auto_checkin && !ctx->in_checkin) {
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

/* -------------------------------------------------------------------------- */
/* Token acquisition (the REQ-AUTH-001 flow)                                  */
/* -------------------------------------------------------------------------- */

static ehem_rc acquire_token(ehem_ctx *ctx, struct ehem_auth *a,
                             const char *scope, int64_t now,
                             const char **token_out)
{
    ehem_json *challenge = NULL;
    ehem_json *result = NULL;
    const char *eid, *spk, *jti, *lbl, *token;
    int64_t challenge_exp, real_exp;
    uint8_t peer_pub[EHEM_X25519_KEYSIZE];
    uint8_t seed[EHEM_X25519_KEYSIZE];
    uint8_t priv[EHEM_X25519_KEYSIZE];
    uint8_t user_pub[EHEM_X25519_KEYSIZE];
    uint8_t shared[EHEM_X25519_KEYSIZE];
    char *ejwt = NULL;
    char *body = NULL;
    struct token_entry *entry;
    ehem_rc rc;

    rc = fetch_challenge(ctx, &challenge);
    if (rc != EHEM_OK) {
        return rc;   /* last-error already recorded by the request path */
    }

    /* Required challenge fields. */
    if (!ehem_json_get_string(challenge, "eid", &eid) ||
        !ehem_json_get_string(challenge, "spk", &spk) ||
        !ehem_json_get_string(challenge, "jti", &jti) ||
        !ehem_json_get_int64(challenge, "exp", &challenge_exp)) {
        ehem_json_free(challenge);
        return ehem_ctx_fail(ctx, EHEM_ERR_PROTOCOL, 0, NULL,
                             AUTH_TOKEN_PATH ": malformed challenge "
                             "(need eid, spk, jti, exp)");
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
        rc = ehem_ejwt_build(jti, spk, challenge_exp, scope, user_pub, shared,
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

    /* Need to acquire; only possible while a credential is retained. */
    if (a == NULL || a->passphrase == NULL) {
        return ehem_ctx_fail(ctx, EHEM_ERR_AUTH_EXPIRED, 0, NULL,
                             "no credential retained for scope '%s' — call "
                             "ehem_login()", scope);
    }
    return acquire_token(ctx, a, scope, now, token_out);
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
    return EHEM_OK;
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
