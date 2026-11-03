/*
 * main.c — the out-of-tree consumer of REQ-BUILD-011's gate. The project that
 * builds it added the SDK as a CMake subdirectory (its tests and shared
 * variant off, its test support on) and linked encedo-hem::encedo-hem-static
 * and ehem::test-support. It reaches one hidden seam (the fast-KDF knob the
 * tool repository's tests depend on) and drives one request through the fake
 * transport — what encedo-hem-tool's unit tests do against the submodule.
 *
 * verifies: REQ-BUILD-011
 */
#include <stdio.h>

#include "ehem/ehem.h"
#include "ehem/system.h"
#include "fake_transport.h"   /* ehem::test-support */
#include "proto_auth.h"       /* src/: the hidden seam ehem_auth_test_set_kdf_iters */

/* The required status fields only (the unit suite's STATUS_MINIMAL). */
static const char STATUS_MINIMAL[] =
    "{ \"ctx\": 2, \"fls_state\": 0, \"uptime\": 42, \"temp\": 40,"
    "  \"storage\": [] }";

int main(void)
{
    ehem_transport *fake;
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_rc rc;
    int ok = 0;

    /* The seam is reachable through the static link (hidden visibility). */
    ehem_auth_test_set_kdf_iters(1);

    fake = fake_transport_new();
    if (fake == NULL ||
        fake_transport_push_response(fake, EHEM_OK, 200, STATUS_MINIMAL) != 0) {
        fprintf(stderr, "subdir_consumer: fake transport: out of memory\n");
        fake_transport_free(fake);
        return 1;
    }
    ehem_options_init(&opts);
    opts.transport = fake;
    rc = ehem_ctx_create("https://device.invalid", &opts, &ctx);
    if (rc != EHEM_OK) {
        fprintf(stderr, "subdir_consumer: ehem_ctx_create: %s\n", ehem_rc_str(rc));
        fake_transport_free(fake);
        return 1;
    }

    rc = ehem_system_status(ctx, &st);
    if (rc == EHEM_OK && st != NULL && st->uptime == 42 &&
        fake_transport_request_count(fake) == 1) {
        printf("subdir_consumer: OK (encedo-hem %s; one request through the fake "
               "transport, uptime %ld)\n", ehem_version(), (long)st->uptime);
        ok = 1;
    } else {
        fprintf(stderr, "subdir_consumer: status through the fake transport: %s (%s)\n",
                ehem_rc_str(rc), ehem_last_error(ctx)->message);
    }

    if (st != NULL) {
        ehem_system_status_free(st);
    }
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
    return ok ? 0 : 1;
}
