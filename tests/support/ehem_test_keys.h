/*
 * ehem_test_keys.h — EHEMTEST key hygiene for integration/disruptive tests.
 *
 * supports: REQ-TEST-003 (device-key mutations confined to the reserved
 *           `EHEMTEST` label prefix; every created key cleaned up pass-or-fail;
 *           a prefix sweep clears leftovers from interrupted runs)
 *
 * Header-only and PUBLIC-API-only (like integration_env.h) so it works for
 * integration tests that link the shared library. A test:
 *   1. mints labels with ehem_test_label() (always EHEMTEST-prefixed),
 *   2. registers each created kid with ehem_test_track(),
 *   3. calls ehem_test_cleanup() from its teardown (runs whether the body
 *      passed or failed), and optionally
 *   4. calls ehem_test_sweep() in setup to remove stray EHEMTEST keys.
 *
 * No routine here ever mutates a key whose label lacks the EHEMTEST prefix, so
 * the protected set (REQ-TOOL-005) is out of bounds by construction.
 */
#ifndef EHEM_TEST_KEYS_H
#define EHEM_TEST_KEYS_H

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ehem/ehem.h"
#include "ehem/keymgmt.h"

/* Reserved test-key label prefix (ARCHITECTURE.md §9). */
#define EHEM_TEST_LABEL_PREFIX "EHEMTEST"

/* Upper bound on kids one test tracks for cleanup (tests create only a few). */
#define EHEM_TEST_MAX_TRACKED 32

/* Registry of kids a test created, drained by ehem_test_cleanup(). */
typedef struct ehem_test_keyreg {
    char   kids[EHEM_TEST_MAX_TRACKED][EHEM_KID_HEX_SIZE];
    size_t count;
} ehem_test_keyreg;

/*
 * Mint a unique EHEMTEST-prefixed label into `buf` (cap ≥ 32). Unique per call
 * within a run — prefix + wall-clock seconds + a monotonic counter — and always
 * within the device's 31-byte label limit (worst case ~26 chars).
 */
static inline void ehem_test_label(char *buf, size_t cap)
{
    static unsigned counter = 0;
    unsigned long t = (unsigned long)time(NULL);
    snprintf(buf, cap, EHEM_TEST_LABEL_PREFIX "-%06lu-%u",
             t % 1000000UL, counter++);
}

/* Register a created kid for later cleanup (silently drops when full). */
static inline void ehem_test_track(ehem_test_keyreg *reg, const char *kid)
{
    if (reg->count < EHEM_TEST_MAX_TRACKED) {
        snprintf(reg->kids[reg->count], EHEM_KID_HEX_SIZE, "%s", kid);
        reg->count++;
    }
}

/*
 * Delete every tracked kid, best-effort: a kid already gone (EHEM_ERR_NOT_FOUND)
 * is fine, any other failure is logged but does not stop the sweep. Safe to call
 * from a teardown path, pass or fail. Empties the registry.
 */
static inline void ehem_test_cleanup(ehem_ctx *ctx, ehem_test_keyreg *reg)
{
    size_t i;
    for (i = 0; i < reg->count; i++) {
        ehem_rc rc = ehem_key_delete(ctx, reg->kids[i]);
        if (rc != EHEM_OK && rc != EHEM_ERR_NOT_FOUND) {
            fprintf(stderr, "[ehem_test] cleanup: delete %s failed: %s (%s)\n",
                    reg->kids[i], ehem_rc_str(rc), ehem_last_error(ctx)->message);
        }
    }
    reg->count = 0;
}

/*
 * Delete every key in the repository whose label starts with EHEMTEST, clearing
 * leftovers from interrupted earlier runs. Returns the number swept, or -1 on a
 * listing failure. Never touches a non-EHEMTEST key.
 */
static inline int ehem_test_sweep(ehem_ctx *ctx)
{
    ehem_key_page *page = NULL;
    size_t plen = strlen(EHEM_TEST_LABEL_PREFIX);
    size_t i;
    int swept = 0;

    if (ehem_key_list_all(ctx, &page) != EHEM_OK) {
        return -1;
    }
    for (i = 0; i < page->listed; i++) {
        const ehem_key_entry *e = &page->entries[i];
        if (e->label != NULL &&
            strncmp(e->label, EHEM_TEST_LABEL_PREFIX, plen) == 0) {
            if (ehem_key_delete(ctx, e->kid) == EHEM_OK) {
                swept++;
            }
        }
    }
    ehem_key_page_free(page);
    return swept;
}

#endif /* EHEM_TEST_KEYS_H */
