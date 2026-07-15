/*
 * integration_env.h — shared gating + setup for `integration`-labeled tests.
 *
 * supports: REQ-TEST-002 (skip, don't fail, when no device is configured)
 *
 * Header-only and PUBLIC-API-only (integration tests link the shared library
 * and must not reach into internals). One place decides how an integration
 * test finds the device and how it skips — no per-test copy-paste.
 *
 * Environment:
 *   EHEM_TEST_URL         device base URL; UNSET → the test skips (exit 77).
 *   EHEM_TEST_INSECURE=1  use TLS insecure mode (self-signed lab device).
 *   EHEM_TEST_CACERT=FILE verify against this CA / pinned certificate.
 *   EHEM_TEST_PASSPHRASE  (used from M2 when auth lands.)
 */
#ifndef EHEM_INTEGRATION_ENV_H
#define EHEM_INTEGRATION_ENV_H

#include <stdio.h>
#include <stdlib.h>

#include "ehem/ehem.h"

/* CTest is told (SKIP_RETURN_CODE) that this exit status means "skipped". */
#define EHEM_SKIP_EXIT 77

static inline const char *ehem_test_url(void)        { return getenv("EHEM_TEST_URL"); }
static inline const char *ehem_test_passphrase(void) { return getenv("EHEM_TEST_PASSPHRASE"); }

/*
 * Call at the top of an integration test's main(): if no device URL is
 * configured, print why and exit with the skip code so CTest reports the test
 * as skipped rather than failed.
 */
static inline void ehem_require_test_url(void)
{
    if (ehem_test_url() == NULL) {
        fprintf(stderr,
                "EHEM_TEST_URL not set — skipping integration test "
                "(set it to a HEM device URL to run)\n");
        exit(EHEM_SKIP_EXIT);
    }
}

/*
 * Create a context for the configured test device, applying the TLS mode the
 * environment asks for. Returns the ehem_ctx_create() result.
 */
static inline ehem_rc ehem_test_ctx(ehem_ctx **out)
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
    return ehem_ctx_create(ehem_test_url(), &opts, out);
}

#endif /* EHEM_INTEGRATION_ENV_H */
