/*
 * hem-tool — a thin CLI over the Encedo HEM C SDK public API.
 *
 * implements: REQ-TOOL-001 (the `status` subcommand),
 *             REQ-TOOL-002 (certificate-refresh notice, `checkin` subcommand)
 *
 * Consumes ONLY the public headers in include/ehem/ — it doubles as living
 * documentation of the API and as the manual driver for the M1 gate. Argument
 * parsing is dependency-free; the subcommand table is structured so `keys list`
 * / `keys rm` (M3) slot in without reworking main().
 *
 * Usage:
 *   hem-tool [--url URL] [--cacert FILE | --insecure] <status|checkin>
 * Connection URL comes from --url or the EHEM_URL environment variable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/ehem.h"
#include "ehem/system.h"

typedef struct {
    const char *url;
    const char *cacert;
    int         insecure;
} cli_opts;

static void usage(FILE *f)
{
    fprintf(f,
        "hem-tool " /* version printed at runtime below */ "\n"
        "usage: hem-tool [options] <command>\n"
        "\n"
        "options:\n"
        "  --url URL        device base URL (or set EHEM_URL)\n"
        "  --cacert FILE    verify TLS against this CA/pinned certificate\n"
        "  --insecure       skip TLS verification (lab use only)\n"
        "  -h, --help       show this help\n"
        "\n"
        "commands:\n"
        "  status           print device status and version\n"
        "  checkin          run the check-in handshake (refreshes the device\n"
        "                   TLS certificate and clock via the Encedo cloud)\n");
}

/* REQ-TOOL-002: a security-relevant event (the device presented an invalid
 * certificate; the SDK refreshed it via check-in) must be visible. */
static void print_cert_notice(const ehem_ctx *ctx)
{
    if (ehem_cert_refreshed(ctx)) {
        fprintf(stderr,
                "notice: device TLS certificate was invalid (expired) — "
                "refreshed via check-in; connection re-verified\n");
    }
}

/* Print the last-error detail recorded on the context to stderr. */
static void print_last_error(ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(stderr, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(stderr, "  detail: %s\n", e->message);
        }
        if (e->http_status != 0) {
            fprintf(stderr, "  http status: %ld\n", e->http_status);
        }
        if (e->device_payload != NULL) {
            fprintf(stderr, "  device: %s\n", e->device_payload);
        }
    }
}

/* Create a context from the CLI connection options. Returns 0 and writes *out
 * on success; nonzero exit code otherwise (message already printed). */
static int make_ctx(const cli_opts *o, ehem_ctx **out)
{
    ehem_options opts;
    ehem_rc rc;

    if (o->url == NULL || o->url[0] == '\0') {
        fprintf(stderr, "error: no device URL — pass --url or set EHEM_URL\n");
        return 2;
    }

    ehem_options_init(&opts);
    if (o->insecure) {
        opts.tls_mode = EHEM_TLS_INSECURE;
    } else if (o->cacert != NULL) {
        opts.tls_mode = EHEM_TLS_CA_FILE;
        opts.ca_file  = o->cacert;
    }

    rc = ehem_ctx_create(o->url, &opts, out);
    if (rc != EHEM_OK) {
        fprintf(stderr, "error: invalid URL or options: %s\n", ehem_rc_str(rc));
        return 1;
    }
    return 0;
}

static int cmd_status(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_version_info *ver = NULL;
    ehem_rc rc;
    size_t i;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    rc = ehem_system_status(ctx, &st);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "system status");
        ehem_ctx_destroy(ctx);
        return 1;
    }
    rc = ehem_system_version(ctx, &ver);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "system version");
        ehem_system_status_free(st);
        ehem_ctx_destroy(ctx);
        return 1;
    }

    printf("Device: %s\n", o->url);
    if (st->hostname != NULL) {
        printf("  hostname:   %s\n", st->hostname);
    }
    printf("  uptime:     %lld s\n", (long long)st->uptime);
    printf("  temp:       %.1f C\n", st->temp);
    if (st->storage_count > 0) {
        printf("  storage:    ");
        for (i = 0; i < st->storage_count; i++) {
            printf("%s%s", (i > 0) ? ", " : "", st->storage[i]);
        }
        printf("\n");
    }
    if (st->has_inited) {
        printf("  inited:     %s\n", st->inited ? "yes" : "no");
    }
    if (st->has_https) {
        printf("  https:      %s\n", st->https ? "yes" : "no");
    }
    printf("  hardware:   %s\n", ver->hwv);
    printf("  firmware:   %s\n", ver->fwv);
    printf("  bootloader: %s\n", ver->blv);

    print_cert_notice(ctx);
    ehem_system_version_free(ver);
    ehem_system_status_free(st);
    ehem_ctx_destroy(ctx);
    return 0;
}

static int cmd_checkin(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    ehem_checkin_info *res = NULL;
    ehem_rc rc;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    rc = ehem_system_checkin(ctx, &res);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "check-in");
        ehem_ctx_destroy(ctx);
        return 1;
    }

    printf("Check-in with %s completed\n", o->url);
    if (res->status != NULL) {
        printf("  status:           %s\n", res->status);
    }
    printf("  cert refreshed:   %s\n", res->cert_updated ? "yes" : "no");
    if (res->newfws != NULL && res->newfws[0] != '\0') {
        printf("  firmware update:  available (%s)\n", res->newfws);
    }
    if (res->newuis != NULL && res->newuis[0] != '\0') {
        printf("  manager update:   available (%s)\n", res->newuis);
    }

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    return 0;
}

int main(int argc, char **argv)
{
    cli_opts o;
    const char *cmd = NULL;
    int i;
    int ret;

    memset(&o, 0, sizeof o);
    o.url = getenv("EHEM_URL");   /* --url overrides below */

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--url") == 0 && i + 1 < argc) {
            o.url = argv[++i];
        } else if (strncmp(a, "--url=", 6) == 0) {
            o.url = a + 6;
        } else if (strcmp(a, "--cacert") == 0 && i + 1 < argc) {
            o.cacert = argv[++i];
        } else if (strncmp(a, "--cacert=", 9) == 0) {
            o.cacert = a + 9;
        } else if (strcmp(a, "--insecure") == 0) {
            o.insecure = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else if (cmd == NULL) {
            cmd = a;
        } else {
            fprintf(stderr, "error: unexpected argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }

    if (o.cacert != NULL && o.insecure) {
        fprintf(stderr, "error: --cacert and --insecure are mutually exclusive\n");
        return 2;
    }
    if (cmd == NULL) {
        usage(stderr);
        return 2;
    }

    if (strcmp(cmd, "status") == 0) {
        ret = cmd_status(&o);
    } else if (strcmp(cmd, "checkin") == 0) {
        ret = cmd_checkin(&o);
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        usage(stderr);
        ret = 2;
    }

    ehem_global_cleanup();
    return ret;
}
