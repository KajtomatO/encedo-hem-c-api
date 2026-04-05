/*
 * test_helpers.h -- shared setup/teardown for integration and destructive tests.
 *
 * Required environment variables:
 *   HEM_TEST_URL         device base URL, e.g. https://my.ence.do
 *   HEM_TEST_PASS        user passphrase
 *   HEM_TEST_MASTER_PASS master passphrase (falls back to HEM_TEST_PASS)
 */
#ifndef HEM_TEST_HELPERS_H
#define HEM_TEST_HELPERS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cmocka.h>

#include "hem/hem.h"

typedef struct {
    hem_ctx_t *ctx;
} test_state_t;

/* Print the last error from ctx to stderr (useful on assertion failure). */
static inline void dump_err(hem_ctx_t *ctx)
{
    fprintf(stderr, "  [error] %s (HTTP %d): %s\n",
            hem_error_string(hem_last_error(ctx)),
            hem_last_http_status(ctx),
            hem_last_error_msg(ctx));
}

/* -------------------------------------------------------------------------
 * Setup: USER role (HEM_TEST_URL + HEM_TEST_PASS)
 * ---------------------------------------------------------------------- */
static int setup_user(void **state)
{
    const char *url  = getenv("HEM_TEST_URL");
    const char *pass = getenv("HEM_TEST_PASS");

    if (!url || !pass) {
        fprintf(stderr, "Set HEM_TEST_URL and HEM_TEST_PASS before running integration tests.\n");
        return -1;
    }

    test_state_t *s = calloc(1, sizeof(test_state_t));
    if (!s) return -1;

    s->ctx = hem_ctx_create(url);
    if (!s->ctx) { free(s); return -1; }

    hem_ctx_set_credentials(s->ctx, pass, HEM_ROLE_USER);
    *state = s;
    return 0;
}

/* -------------------------------------------------------------------------
 * Setup: MASTER role (HEM_TEST_URL + HEM_TEST_MASTER_PASS or HEM_TEST_PASS)
 * ---------------------------------------------------------------------- */
static int setup_master(void **state)
{
    const char *url  = getenv("HEM_TEST_URL");
    const char *pass = getenv("HEM_TEST_MASTER_PASS");
    if (!pass) pass  = getenv("HEM_TEST_PASS");

    if (!url || !pass) {
        fprintf(stderr, "Set HEM_TEST_URL and HEM_TEST_MASTER_PASS (or HEM_TEST_PASS).\n");
        return -1;
    }

    test_state_t *s = calloc(1, sizeof(test_state_t));
    if (!s) return -1;

    s->ctx = hem_ctx_create(url);
    if (!s->ctx) { free(s); return -1; }

    hem_ctx_set_credentials(s->ctx, pass, HEM_ROLE_MASTER);
    *state = s;
    return 0;
}

/* -------------------------------------------------------------------------
 * Setup: NO credentials (device init -- passphrases passed per-test)
 * ---------------------------------------------------------------------- */
static int setup_noauth(void **state)
{
    const char *url = getenv("HEM_TEST_URL");

    if (!url) {
        fprintf(stderr, "Set HEM_TEST_URL before running integration tests.\n");
        return -1;
    }

    test_state_t *s = calloc(1, sizeof(test_state_t));
    if (!s) return -1;

    s->ctx = hem_ctx_create(url);
    if (!s->ctx) { free(s); return -1; }

    *state = s;
    return 0;
}

static int teardown_ctx(void **state)
{
    test_state_t *s = *state;
    if (s) {
        hem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

/* Convenience macro: assert HEM_OK and print error detail on failure. */
#define assert_hem_ok(ctx, err) \
    do { \
        if ((err) != HEM_OK) { dump_err(ctx); } \
        assert_int_equal((err), HEM_OK); \
    } while (0)

/* Assert that an operation returns a specific non-OK error. */
#define assert_hem_err(ctx, err, expected) \
    do { \
        if ((err) == HEM_OK || (err) != (expected)) { dump_err(ctx); } \
        assert_int_equal((err), (expected)); \
    } while (0)

#endif /* HEM_TEST_HELPERS_H */
