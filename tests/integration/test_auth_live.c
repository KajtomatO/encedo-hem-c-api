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

    printf("[test_auth_live] scope=%s sub=%s — login/token round-trip OK\n",
           scope_claim, sub);

    /* NOTE: no same-scope cache-reuse assertion here. This DIAG firmware ties
     * the bearer's `exp` to the challenge's ~60 s response deadline (TTL ≈ 59 s
     * observed), which is below the 60 s cache skew, so a just-issued token is
     * already at (or past) its skew boundary — whether a second ensure_token
     * hits the cache or re-acquires depends on sub-second device/host clock
     * skew. The reference python client behaves identically. Cache reuse is
     * proven deterministically in the unit suite (test_auth.c) with a pinned
     * clock; here we only assert the live login/derivation round-trip. `token`
     * is borrowed and must not be used past another auth op (it is not). */
    ehem_json_free(payload);
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
        cmocka_unit_test(test_live_token_scope_claim),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
