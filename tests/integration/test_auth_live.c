/*
 * test_auth_live.c — live authenticated token acquisition against the real
 * dev-machine HEM.
 *
 * verifies: REQ-AUTH-001 / REQ-AUTH-002 / REQ-AUTH-003 end-to-end — a real
 *           passphrase login (challenge → PBKDF2/X25519/ECDH → eJWT POST)
 *           yields a device-signed bearer whose `scope` claim is the one we
 *           requested; records the `sub` claim for the REQ-AUTH-001 M2-gate
 *           criterion. Gated on EHEM_TEST_URL + EHEM_TEST_PASSPHRASE
 *           (REQ-TEST-002): skipped (exit 77) when either is unset.
 *           REQ-AUTH-005: a checkin_on_login context runs the proactive
 *           check-in (device clock resync) and the login still lands.
 *           (REQ-AUTH-004's drift recovery is unit-proven; live it fires
 *           opportunistically inside these logins whenever the device has
 *           actually drifted past the TTL — no way to fabricate drift
 *           remotely.)
 *
 * EXCEPTION to the "integration tests use only the public API" rule: there is
 * no public authenticated binding until M2-050, so this test drives the
 * internal ehem_auth_ensure_token() directly and links the static library
 * (like the unit suite). It is replaced/augmented by a public-API live test
 * once a scoped binding (config GET) exists.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "proto_auth.h"        /* internal: ehem_auth_ensure_token */
#include "ejwt.h"              /* internal: base64url decode */
#include "json.h"              /* internal: parse the bearer payload */
#include "integration_env.h"

/* A read-only scope every provisioned user key can obtain. */
#define LIVE_SCOPE "keymgmt:list"

/* Decode a bearer JWT's payload segment into a parsed JSON object (caller frees
 * with ehem_json_free), or NULL on any structural failure. */
static ehem_json *decode_bearer_payload(const char *token)
{
    const char *d1 = strchr(token, '.');
    const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    uint8_t raw[2048];
    size_t n;
    if (d1 == NULL || d2 == NULL) {
        return NULL;
    }
    n = ehem_b64url_decode(d1 + 1, (size_t)(d2 - (d1 + 1)), raw, sizeof raw);
    if (n == (size_t)-1) {
        return NULL;
    }
    return ehem_json_parse((const char *)raw, n);
}

static void test_live_token_scope_claim(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    const char *token = NULL;
    const char *scope_claim = NULL, *sub = NULL;
    int64_t iat = 0, exp = 0, ttl;
    ehem_json *payload;
    ehem_rc rc;

    rc = ehem_test_ctx(&ctx);
    assert_int_equal(rc, EHEM_OK);
    assert_non_null(ctx);

    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_auth_ensure_token(ctx, LIVE_SCOPE, &token);
    if (rc != EHEM_OK) {
        fail_msg("ehem_auth_ensure_token(%s) failed: %s (%s)", LIVE_SCOPE,
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(token);

    payload = decode_bearer_payload(token);
    assert_non_null(payload);                 /* a well-formed 3-segment JWT */

    /* The device echoes the requested scope and stamps the derivation identity
     * in `sub` (U = User-key/PBKDF2, M = Manager/Argon2 — see REQ-AUTH-001). */
    assert_true(ehem_json_get_string(payload, "scope", &scope_claim));
    assert_string_equal(scope_claim, LIVE_SCOPE);
    assert_true(ehem_json_get_string(payload, "sub", &sub));

    /* The token is LONG-LIVED, not the ~60 s challenge deadline: STEP-M2-045
     * stopped capping the eJWT `exp` at challenge.exp, so the device grants the
     * full requested lifetime (~3600 s). Before the fix this was ~59 s. */
    assert_true(ehem_json_get_int64(payload, "iat", &iat));
    assert_true(ehem_json_get_int64(payload, "exp", &exp));
    ttl = exp - iat;
    printf("[test_auth_live] scope=%s sub=%s TTL=%llds — long-lived token OK\n",
           scope_claim, sub, (long long)ttl);
    assert_true(ttl >= 3000);   /* ~3600 in practice; far above the 60 s deadline */

    /* TTL >> the 60 s skew now, so same-scope reuse reliably hits the cache: a
     * second ensure returns the SAME token with no re-login (REQ-AUTH-002). A
     * cache hit does not re-acquire, so `token` stays valid across this call. */
    const char *token2 = NULL;
    assert_int_equal(ehem_auth_ensure_token(ctx, LIVE_SCOPE, &token2), EHEM_OK);
    assert_string_equal(token, token2);

    ehem_json_free(payload);
    assert_int_equal(ehem_logout(ctx), EHEM_OK);
    ehem_ctx_destroy(ctx);
}

/* Like ehem_test_ctx(), plus checkin_on_login (REQ-AUTH-005). */
static ehem_rc test_ctx_checkin_on_login(ehem_ctx **out)
{
    ehem_options opts;
    const char *cacert   = getenv("EHEM_TEST_CACERT");
    const char *insecure = getenv("EHEM_TEST_INSECURE");

    ehem_options_init(&opts);
    if (insecure != NULL && insecure[0] == '1') {
        opts.tls_mode = EHEM_TLS_INSECURE;
    } else if (cacert != NULL) {
        opts.tls_mode = EHEM_TLS_CA_FILE;
        opts.ca_file  = cacert;
    }
    ehem_test_apply_pace(&opts);
    opts.checkin_on_login = 1;
    return ehem_ctx_create(ehem_test_url(), &opts, out);
}

/* checkin_on_login: the proactive check-in runs (3-leg handshake against the
 * real device + cloud relay, resyncing the device RTC) and the first token
 * acquisition on the context still succeeds. */
static void test_live_checkin_on_login(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    const char *token = NULL;
    ehem_rc rc;

    assert_int_equal(test_ctx_checkin_on_login(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_auth_ensure_token(ctx, LIVE_SCOPE, &token);
    if (rc != EHEM_OK) {
        fail_msg("checkin_on_login ensure_token failed: %s (%s)",
                 ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }
    assert_non_null(token);
    /* A successful proactive check-in leaves no breadcrumb; a best-effort
     * failure would (and must not have failed the login). Either way we log
     * what happened for the evidence record. */
    printf("[test_auth_live] checkin_on_login: login OK; note='%s'\n",
           ehem_last_error(ctx)->message);

    assert_int_equal(ehem_logout(ctx), EHEM_OK);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    /* No device or no passphrase → skip (reported skipped, not failed). */
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live auth test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        /* checkin_on_login runs FIRST deliberately: its check-in resyncs the
         * ~8%-fast device RTC, so the TTL assertion in the scope-claim test is
         * immune to accumulated drift (observed live: 24 min of drift shrank
         * the device-stamped TTL to 2164 s — the exact pathology REQ-AUTH-005
         * exists to heal). */
        cmocka_unit_test(test_live_checkin_on_login),
        cmocka_unit_test(test_live_token_scope_claim),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
