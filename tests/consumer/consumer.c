/*
 * consumer.c — the smallest application of an INSTALLED encedo-hem, built by
 * the consumer gate (check_consumer.cmake) once per way in: public headers
 * only, a few public calls and a context for a closed local port — no
 * traffic; that it compiles, links and starts against the install is the
 * test. It prints the SDK version and whether the build gave it
 * EHEM_USING_SHARED, which the gate compares with the way it was linked.
 *
 * supports: REQ-BUILD-008
 */
#include <stdio.h>

#include "ehem/ehem.h"

int main(void)
{
#ifdef EHEM_USING_SHARED
    const char *define = "EHEM_USING_SHARED";
#else
    const char *define = "no EHEM_USING_SHARED";
#endif
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_rc rc;

    rc = ehem_global_init();
    if (rc != EHEM_OK) {
        fprintf(stderr, "ehem_global_init: %s\n", ehem_rc_str(rc));
        return 1;
    }
    ehem_options_init(&opts);
    rc = ehem_ctx_create("http://127.0.0.1:1", &opts, &ctx);
    if (rc != EHEM_OK || ctx == NULL) {
        fprintf(stderr, "ehem_ctx_create: %s\n", ehem_rc_str(rc));
        ehem_global_cleanup();
        return 1;
    }
    ehem_ctx_destroy(ctx);
    ehem_global_cleanup();
    printf("encedo-hem %s, %s\n", ehem_version(), define);
    return 0;
}
