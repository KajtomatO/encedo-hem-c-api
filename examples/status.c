/*
 * status.c — the smallest useful SDK program: the device's status under the
 * system trust store, and the SDK's error detail when it fails.
 *
 * implements: REQ-API-007 (the runnable examples; until the SDK's 1.2.1 that
 *             role was hem-tool's, now encedo-hem-tool's)
 *
 *   status [URL]        URL defaults to $EHEM_URL, then https://my.ence.do
 *
 * The shape of every read-only call: ehem_global_init() once, options from
 * ehem_options_init() (the defaults: system trust, automatic recoveries on),
 * one context per device, the call, the *_free() of its result, the context
 * destroyed. Exit 0 with the status printed; 1 with the ehem_rc name, the
 * last-error message and the HTTP status on stderr — plus a line when the
 * failure was the one classifiable TLS case, an EXPIRED device certificate
 * (REQ-NET-005; the SDK already tried the check-in recovery by then).
 * Public API only, as any consumer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/ehem.h"
#include "ehem/system.h"

static const char *device_url(int argc, char **argv)
{
    const char *env = getenv("EHEM_URL");
    if (argc > 1) {
        return argv[1];
    }
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    return "https://my.ence.do";
}

/* Print why a call failed the way the SDK reports it; returns the exit code. */
static int report(ehem_ctx *ctx, const char *what, ehem_rc rc)
{
    const ehem_error *err = ehem_last_error(ctx);

    fprintf(stderr, "status: %s: %s", what, ehem_rc_str(rc));
    if (err != NULL && err->message[0] != '\0') {
        fprintf(stderr, " - %s", err->message);
    }
    if (err != NULL && err->http_status != 0 && strstr(err->message, "HTTP") == NULL) {
        fprintf(stderr, " (HTTP %ld)", err->http_status);
    }
    fputc('\n', stderr);
    if (err != NULL && err->tls_expired) {
        fprintf(stderr, "        the device certificate has EXPIRED: the automatic "
                        "check-in ran (ehem_options.no_auto_checkin is off) and "
                        "the device still serves the old certificate\n");
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *url = device_url(argc, argv);
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_rc rc;
    size_t i;

    rc = ehem_global_init();
    if (rc != EHEM_OK) {
        /* The one refusal before any call: the crypto library the process
         * loaded is not the one this SDK was built against (REQ-API-010). */
        ehem_build_info bi;
        ehem_build_info_get(&bi);
        fprintf(stderr, "status: ehem_global_init: %s - %s built against %s, "
                        "loaded %s\n", ehem_rc_str(rc), bi.crypto_backend,
                bi.crypto_version_compiled, bi.crypto_version_loaded);
        return 1;
    }

    ehem_options_init(&opts);          /* required before overriding anything */
    /* opts.tls_mode stays EHEM_TLS_SYSTEM: the device's certificate must chain
     * to the system trust store (a lab device: EHEM_TLS_CA_FILE + ca_file). */
    rc = ehem_ctx_create(url, &opts, &ctx);
    if (rc != EHEM_OK) {
        fprintf(stderr, "status: ehem_ctx_create(%s): %s\n", url, ehem_rc_str(rc));
        ehem_global_cleanup();
        return 1;
    }

    rc = ehem_system_status(ctx, &st);
    if (rc != EHEM_OK) {
        int code = report(ctx, "ehem_system_status", rc);
        ehem_ctx_destroy(ctx);
        ehem_global_cleanup();
        return code;
    }

    printf("device:   %s\n", url);
    if (st->hostname != NULL) {
        printf("hostname: %s\n", st->hostname);
    }
    printf("uptime:   %ld s\n", (long)st->uptime);
    printf("temp:     %.1f C\n", st->temp);
    printf("state:    %s (fls_state %ld, ctx %ld)\n",
           st->fls_state == 0 ? "no errors" : "FAIL-STATE",
           (long)st->fls_state, (long)st->ctx);
    if (st->has_inited) {
        printf("inited:   %s\n", st->inited ? "yes" : "no");
    }
    if (st->has_https) {
        printf("https:    %s\n", st->https ? "yes" : "no");
    }
    if (st->has_tts) {
        printf("tts:      %s (trusted time)\n", st->tts ? "yes" : "no");
    }
    if (st->ts != NULL) {
        printf("time:     %s\n", st->ts);
    }
    for (i = 0; i < st->storage_count; i++) {
        printf("storage:  %s\n", st->storage[i]);
    }
    if (ehem_cert_refreshed(ctx)) {
        printf("note:     the device certificate was refreshed by an automatic check-in\n");
    }

    ehem_system_status_free(st);
    ehem_ctx_destroy(ctx);
    ehem_global_cleanup();
    return 0;
}
