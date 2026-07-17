/*
 * test_logger_live.c — live audit-log access against the real dev-machine
 * HEM.
 *
 * verifies: REQ-SYS-009 (live: logger key triple fetched AND the nonce
 *           signature VERIFIED locally with the crypto shim's Ed25519 —
 *           proof the device holds the log-signing private key; list page 0
 *           → download the first file → non-empty line-oriented text;
 *           PPA/EPA routing recorded — the dev device is PPA per the
 *           REQ-SYS-007 se_state finding, so list/get are expected live)
 *
 * Links the STATIC lib + internal headers (ehem_test_support) for the shim
 * Ed25519 verify — the same exception as test_ecdh_live.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/logger.h"
#include "crypto_shim.h"
#include "integration_env.h"

static void test_logger_key_signature(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_logger_key_info *info = NULL;
    int valid = 0;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_logger_key(ctx, &info);
    if (rc != EHEM_OK) {
        fail_msg("ehem_logger_key failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(ctx)->message);
    }

    /* The device must prove it holds the log-signing private key: verify
     * nonce_signed over nonce with the returned public key (shim Ed25519). */
    rc = ehem_ed25519_verify(info->key, info->nonce, EHEM_LOGGER_NONCE_SIZE,
                             info->nonce_signed, EHEM_LOGGER_SIG_SIZE, &valid);
    assert_int_equal(rc, EHEM_OK);
    assert_true(valid);
    printf("[test_logger_live] log-signing key Ed25519 nonce proof VERIFIED\n");

    ehem_logger_key_free(info);
    ehem_logout(ctx);
    ehem_ctx_destroy(ctx);
}

static void test_logger_list_and_get(void **state)
{
    (void)state;
    ehem_ctx *ctx = NULL;
    ehem_logger_page *page = NULL;
    uint8_t *data = NULL;
    size_t len = 0;
    ehem_rc rc;

    assert_int_equal(ehem_test_ctx(&ctx), EHEM_OK);
    assert_int_equal(ehem_login(ctx, ehem_test_passphrase()), EHEM_OK);

    rc = ehem_logger_list(ctx, 0, &page);
    if (rc == EHEM_ERR_NOT_FOUND) {
        /* EPA build (or zero logs): recorded, skipped. The dev device is PPA
         * (REQ-SYS-007 se_state), so this path is unexpected there. */
        printf("[test_logger_live] list → NOT_FOUND (EPA build or no logs) — "
               "recorded for REQ-SYS-009\n");
        ehem_logout(ctx);
        ehem_ctx_destroy(ctx);
        skip();
        return;
    }
    if (rc != EHEM_OK) {
        fail_msg("ehem_logger_list failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(ctx)->message);
    }
    printf("[test_logger_live] list: total=%lld page_count=%u first=%s\n",
           (long long)page->total, (unsigned)page->count,
           page->count > 0 ? page->ids[0] : "(none)");
    assert_true(page->total >= 1);   /* the suite's own audit events exist */
    assert_true(page->count >= 1);

    rc = ehem_logger_get(ctx, page->ids[0], &data, &len);
    if (rc == EHEM_ERR_DEVICE &&
        ehem_last_error(ctx)->http_status == 406) {
        /* FR_LOCKED: the newest file is the currently-open log — recorded;
         * try the next one when available. */
        printf("[test_logger_live] get %s → 406 FR_LOCKED (current log)\n",
               page->ids[0]);
        if (page->count > 1) {
            rc = ehem_logger_get(ctx, page->ids[1], &data, &len);
        }
    }
    if (rc != EHEM_OK) {
        fail_msg("ehem_logger_get failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(ctx)->message);
    }
    assert_true(len > 0);
    /* FINDING (2026-07-18, device > doc): records are PIPE-DELIMITED lines
     * ("seq|ts|type|result|…|sig|chain", base64url fields) under a
     * "# Encedo nGINE FW …" header — NOT the doc's "JSON-like record per
     * line". Recorded in REQ-SYS-009; assert the real shape. */
    assert_non_null(memchr(data, '|', len));
    assert_memory_equal(data, "# Encedo", 8);
    printf("[test_logger_live] get: %u bytes; first line: %.60s...\n",
           (unsigned)len, (const char *)data);

    ehem_logger_file_free(data);
    ehem_logger_page_free(page);
    ehem_logout(ctx);
    ehem_ctx_destroy(ctx);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_logger_key_signature),
        cmocka_unit_test(test_logger_list_and_get),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
