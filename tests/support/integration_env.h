/*
 * integration_env.h — shared gating + setup for `integration`-labeled tests.
 *
 * supports: REQ-TEST-002 (skip, don't fail, when no device is configured),
 *           REQ-TEST-005 (opt-in per-test device reboot — stall mitigation)
 *
 * Header-only and PUBLIC-API-only (integration tests link the shared library
 * and must not reach into internals). One place decides how an integration
 * test finds the device and how it skips — no per-test copy-paste.
 *
 * Environment:
 *   EHEM_TEST_URL          device base URL; UNSET → the test skips (exit 77).
 *   EHEM_TEST_INSECURE=1   use TLS insecure mode (self-signed lab device).
 *   EHEM_TEST_CACERT=FILE  verify against this CA / pinned certificate.
 *   EHEM_TEST_PASSPHRASE   login passphrase for authenticated tests.
 *   EHEM_TEST_PACE_MS      client-side request pacing (REQ-NET-006).
 *   EHEM_TEST_REBOOT_EACH=1  reboot the device before each test binary
 *                          (REQ-TEST-005; needs the passphrase; see
 *                          `./dev test it --reboot-each`).
 */
#ifndef EHEM_INTEGRATION_ENV_H
#define EHEM_INTEGRATION_ENV_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/system.h"

/* CTest is told (SKIP_RETURN_CODE) that this exit status means "skipped". */
#define EHEM_SKIP_EXIT 77

static inline const char *ehem_test_url(void)        { return getenv("EHEM_TEST_URL"); }
static inline const char *ehem_test_passphrase(void) { return getenv("EHEM_TEST_PASSPHRASE"); }

/*
 * Apply the optional client-side request pace (REQ-NET-006) from
 * EHEM_TEST_PACE_MS onto `opts`. The dev HEM intermittently stalls under
 * back-to-back load (see KNOWN-ISSUES.md); pacing throttles the suite. The SDK
 * itself reads no environment — the test harness does, and passes it as the
 * option, keeping the "config arrives as parameters" rule.
 */
static inline void ehem_test_apply_pace(ehem_options *opts)
{
    const char *pace = getenv("EHEM_TEST_PACE_MS");
    if (pace != NULL && pace[0] != '\0') {
        opts->request_pace_ms = strtol(pace, NULL, 10);
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
    ehem_test_apply_pace(&opts);
    return ehem_ctx_create(ehem_test_url(), &opts, out);
}

/*
 * Like ehem_test_ctx(), but overrides the whole-request timeout (ms). For live
 * operations the 30 s default is too tight for — notably per-family key
 * generation, where ML-DSA/ML-KEM keygen on the device MCU can run long.
 */
static inline ehem_rc ehem_test_ctx_timeout(ehem_ctx **out, long total_ms)
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
    opts.total_timeout_ms = total_ms;
    ehem_test_apply_pace(&opts);
    return ehem_ctx_create(ehem_test_url(), &opts, out);
}

/*
 * Opt-in per-test device reboot (REQ-TEST-005; supports: REQ-TEST-005): when
 * EHEM_TEST_REBOOT_EACH=1, reboot the device once at test startup and wait
 * until it serves /api/system/status again. A TEMPORARY mitigation for the
 * firmware's stall-under-sustained-load mode (KNOWN-ISSUES; the watchdog is
 * disabled) — rebooting between test binaries resets the firmware state so a
 * full suite run can finish. Needs EHEM_TEST_PASSPHRASE (reboot is scoped);
 * without it the test proceeds un-rebooted with a note. A failed reboot or a
 * device that stays silent past the bounded wait fails FAST (exit 1) so the
 * suite doesn't grind against a wedged device.
 *
 * The wait loop deliberately uses no sleep primitive (none is portable in a
 * shared header under -std=c99): every probe context carries
 * request_pace_ms = 1000, so the SDK's own pacing (REQ-NET-006) spaces the
 * probes ≥ 1 s apart, and an unreachable device adds its connect timeout on
 * top. Two phases: first wait until the OLD instance stops answering (the
 * firmware keeps serving for ~2 s after accepting the reboot — an immediate
 * OK probe would be a false "back"), then wait until status answers again.
 */
static inline void ehem_test_reboot_if_requested(void)
{
    const char *flag = getenv("EHEM_TEST_REBOOT_EACH");
    ehem_ctx *ctx = NULL;
    time_t start, deadline;
    int attempts = 0;
    ehem_rc rc;

    if (flag == NULL || flag[0] != '1') {
        return;
    }
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "[reboot-each] EHEM_TEST_PASSPHRASE not set — "
                        "cannot reboot; proceeding without\n");
        return;
    }

    fprintf(stderr, "[reboot-each] rebooting the device before this test "
                    "(EHEM_TEST_REBOOT_EACH=1)...\n");
    if (ehem_test_ctx(&ctx) != EHEM_OK ||
        ehem_login(ctx, ehem_test_passphrase()) != EHEM_OK) {
        fprintf(stderr, "[reboot-each] context/login setup failed\n");
        ehem_ctx_destroy(ctx);
        exit(1);
    }
    rc = ehem_system_reboot(ctx);
    if (rc != EHEM_OK) {
        fprintf(stderr, "[reboot-each] reboot failed: %s (%s)\n",
                ehem_rc_str(rc), ehem_last_error(ctx)->message);
        ehem_ctx_destroy(ctx);
        exit(1);
    }
    ehem_ctx_destroy(ctx);

    start = time(NULL);
    deadline = start + 180;
    {
        int seen_down = 0;
        for (;;) {
            ehem_options opts;
            ehem_ctx *probe = NULL;
            ehem_status_info *st = NULL;
            const char *cacert   = getenv("EHEM_TEST_CACERT");
            const char *insecure = getenv("EHEM_TEST_INSECURE");

            ehem_options_init(&opts);
            if (insecure != NULL && insecure[0] == '1') {
                opts.tls_mode = EHEM_TLS_INSECURE;
            } else if (cacert != NULL) {
                opts.tls_mode = EHEM_TLS_CA_FILE;
                opts.ca_file  = cacert;
            }
            opts.connect_timeout_ms = 3000;
            opts.total_timeout_ms   = 4000;
            opts.request_pace_ms    = 1000;   /* the portable probe spacing */

            rc = EHEM_ERR_UNREACHABLE;
            if (ehem_ctx_create(ehem_test_url(), &opts, &probe) == EHEM_OK) {
                rc = ehem_system_status(probe, &st);
            }
            if (rc == EHEM_OK) {
                ehem_system_status_free(st);
                st = NULL;
                if (seen_down) {
                    ehem_ctx_destroy(probe);
                    fprintf(stderr, "[reboot-each] device back after ~%ld s\n",
                            (long)(time(NULL) - start));
                    return;
                }
                /* Still the OLD instance (the firmware serves for ~2 s after
                 * accepting the reboot) — keep waiting for it to drop. */
            } else {
                seen_down = 1;
            }
            ehem_ctx_destroy(probe);
            attempts++;
            if (time(NULL) >= deadline || attempts >= 180) {
                fprintf(stderr, "[reboot-each] device did not return within "
                                "~90 s — aborting (power-cycle it?)\n");
                exit(1);
            }
        }
    }
}

/*
 * Call at the top of an integration test's main(): if no device URL is
 * configured, print why and exit with the skip code so CTest reports the test
 * as skipped rather than failed. With EHEM_TEST_REBOOT_EACH=1 it also runs
 * the per-test reboot above (REQ-TEST-005).
 */
static inline void ehem_require_test_url(void)
{
    if (ehem_test_url() == NULL) {
        fprintf(stderr,
                "EHEM_TEST_URL not set — skipping integration test "
                "(set it to a HEM device URL to run)\n");
        exit(EHEM_SKIP_EXIT);
    }
    ehem_test_reboot_if_requested();
}

/*
 * Call at the top of a `disruptive`-labeled test's main(): like
 * ehem_require_test_url(), but ALSO requires EHEM_ALLOW_DISRUPTIVE=1 — an
 * explicit opt-in, because these tests mutate and reboot the device
 * (ARCHITECTURE.md §9). Skips (exit 77) when either gate is missing.
 */
static inline void ehem_require_disruptive(void)
{
    const char *allow = getenv("EHEM_ALLOW_DISRUPTIVE");
    if (allow == NULL || allow[0] != '1') {
        if (ehem_test_url() == NULL) {
            fprintf(stderr,
                    "EHEM_TEST_URL not set — skipping integration test "
                    "(set it to a HEM device URL to run)\n");
            exit(EHEM_SKIP_EXIT);
        }
        fprintf(stderr,
                "EHEM_ALLOW_DISRUPTIVE=1 not set — skipping disruptive test "
                "(it mutates + reboots the device)\n");
        exit(EHEM_SKIP_EXIT);
    }
    ehem_require_test_url();
}

#endif /* EHEM_INTEGRATION_ENV_H */
