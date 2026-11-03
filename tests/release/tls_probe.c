/*
 * tls_probe.c — tls-probe: one request to a URL under a given CA file, and
 * the SDK's verdict on its TLS failure, printed as one line.
 *
 * verifies: REQ-NET-005 (the end-to-end proof driver of the expired-
 *           certificate classifier: scripts/release/check-expired-classifier.sh
 *           runs it against local expired and not-yet-valid endpoints in the
 *           release job under both backends — until M13 that driver was
 *           `hem-tool recovery`, now encedo-hem-tool's)
 *
 * implements: REQ-BUILD-010 (the per-build classifier check)
 *
 *   tls-probe <url> <ca-file>
 *
 * Creates a context for <url> verifying against <ca-file> (EHEM_TLS_CA_FILE)
 * with the automatic check-in recovery OFF — the probe measures the
 * classifier, not the recovery (unit-tested in tests/unit/test_checkin.c; the
 * verdict is set either way, REQ-NET-005), so nothing but this one request
 * leaves the process — calls ehem_system_status() and prints
 *
 *   rc=<EHEM_...> tls_expired=<0|1> detail=<the last-error message>
 *
 * Exit 0 once that line is printed, whatever the verdict (the caller reads
 * the line); 2 on usage; 1 when the SDK could not be initialised or the
 * context not created (the reason on stderr). Public API only — the same
 * program a consumer could build against an installed package. Built
 * whenever the tests are (BUILD_TESTING); not registered as a CTest: it needs
 * the endpoints the script serves.
 */
#include <stdio.h>

#include "ehem/ehem.h"
#include "ehem/system.h"

int main(int argc, char **argv)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    const ehem_error *err;
    ehem_rc rc;

    if (argc != 3) {
        fprintf(stderr, "usage: tls-probe <url> <ca-file>\n");
        return 2;
    }

    rc = ehem_global_init();
    if (rc != EHEM_OK) {
        fprintf(stderr, "tls-probe: ehem_global_init: %s\n", ehem_rc_str(rc));
        return 1;
    }

    ehem_options_init(&opts);
    opts.tls_mode           = EHEM_TLS_CA_FILE;
    opts.ca_file            = argv[2];
    opts.no_auto_checkin    = 1;       /* the verdict only; no check-in, no cloud */
    opts.connect_timeout_ms = 5000;    /* local endpoints: fail fast */
    opts.total_timeout_ms   = 10000;
    rc = ehem_ctx_create(argv[1], &opts, &ctx);
    if (rc != EHEM_OK) {
        fprintf(stderr, "tls-probe: ehem_ctx_create(%s): %s\n", argv[1],
                ehem_rc_str(rc));
        ehem_global_cleanup();
        return 1;
    }

    rc  = ehem_system_status(ctx, &st);
    err = ehem_last_error(ctx);
    printf("rc=%s tls_expired=%d detail=%s\n", ehem_rc_str(rc),
           err->tls_expired, err->message);

    if (st != NULL) {
        ehem_system_status_free(st);
    }
    ehem_ctx_destroy(ctx);
    ehem_global_cleanup();
    return 0;
}
