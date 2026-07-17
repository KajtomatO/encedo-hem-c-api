/*
 * hem-tool — a thin CLI over the Encedo HEM C SDK public API.
 *
 * implements: REQ-TOOL-001 (the `status` subcommand),
 *             REQ-TOOL-002 (certificate-refresh notice, `checkin` subcommand),
 *             REQ-TOOL-003 (the `cert-install` subcommand),
 *             REQ-TOOL-004 (the `keys list` subcommand),
 *             REQ-TOOL-006 (the `keys rm` subcommand),
 *             REQ-TOOL-007 (the `keys pub` subcommand),
 *             REQ-TOOL-008 (the `sign` subcommand),
 *             REQ-TOOL-009 (the `keys gen` subcommand)
 *
 * Consumes ONLY the public headers in include/ehem/ — it doubles as living
 * documentation of the API and as the manual driver for the M1/M2 gates.
 * Argument parsing is dependency-free; the subcommand table is structured so
 * `keys list` / `keys rm` (M3) slot in without reworking main().
 *
 * Usage:
 *   hem-tool [--url URL] [--cacert FILE | --insecure] [--passphrase PW]
 *            [--force] <status|checkin|cert-install>
 * Connection URL comes from --url or the EHEM_URL environment variable; the
 * passphrase from --passphrase or EHEM_PASSPHRASE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/ehem.h"
#include "ehem/system.h"

#include "cert_install.h"
#include "keys.h"
#include "random.h"
#include "sign.h"

#define MAX_LABEL_PREFIXES 32

typedef struct {
    const char *url;
    const char *cacert;
    int         insecure;
    const char *passphrase;   /* --passphrase / EHEM_PASSPHRASE (cert-install, keys) */
    int         force;        /* --force (cert-install) */

    /* keys rm selection/behavior (REQ-TOOL-006). */
    int         all;          /* --all */
    int         dry_run;      /* --dry-run */
    int         assume_yes;   /* --yes */
    const char *prefixes[MAX_LABEL_PREFIXES];  /* --label-prefix (repeatable) */
    size_t      prefix_count;

    /* keys pub / sign output format (REQ-TOOL-007/008). */
    int         hex;          /* --hex */
    int         raw;          /* --raw */

    /* sign inputs (REQ-TOOL-008). */
    const char *alg;          /* --alg (verbatim selector; NULL → default) */
    const char *in_path;      /* --in (message file; NULL → stdin) */
    const char *sigctx;       /* --sigctx (RFC 8032 context string) */

    /* keys gen inputs (REQ-TOOL-009). */
    const char *label;        /* --label (required for keys gen) */
    const char *descr;        /* --descr (optional raw-bytes blob) */
    const char *mode;         /* --mode (ECDH|ExDSA|ECDH,ExDSA; NULL → auto) */

    /* random inputs (REQ-TOOL-010). */
    const char *kid;          /* --kid (existing AES key; NULL → transient) */
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
        "  --passphrase PW  login passphrase (or set EHEM_PASSPHRASE)\n"
        "  --force          cert-install: reinstall even if already current\n"
        "  --all            keys rm: target every non-protected key\n"
        "  --label-prefix P keys rm: target keys whose label starts with P\n"
        "                   (repeatable; exact match required for protected keys)\n"
        "  --dry-run        keys rm: show what would be deleted, delete nothing\n"
        "  --yes            keys rm: skip the bulk prompt (never for protected keys)\n"
        "  --hex            keys pub / sign: print the output as lowercase hex\n"
        "  --raw            keys pub / sign / random: ONLY raw bytes on stdout\n"
        "  --kid KID        random: use this existing AES key (else a transient\n"
        "                   EHEMTEST key is created and removed)\n"
        "  --alg ALG        sign: algorithm selector (e.g. Ed25519,\n"
        "                   SHA256WithECDSA); omitted → derived from the key type\n"
        "  --in FILE        sign: read the message from FILE (default: stdin)\n"
        "  --sigctx STR     sign: RFC 8032 context for the Ed*ctx/Ed*ph selectors\n"
        "  --label LABEL    keys gen: label for the new key (required)\n"
        "  --descr STR      keys gen: optional opaque description blob\n"
        "  --mode MODE      keys gen: ECDH | ExDSA | ECDH,ExDSA (NIST-P/K only;\n"
        "                   default ECDH,ExDSA so the key can sign)\n"
        "  -h, --help       show this help\n"
        "\n"
        "commands:\n"
        "  status           print device status and version\n"
        "  checkin          run the check-in handshake (refreshes the device\n"
        "                   TLS certificate and clock via the Encedo cloud)\n"
        "  cert-install     harvest the cloud certificate and install it on a\n"
        "                   device whose firmware cannot apply it itself\n"
        "                   (authenticates, installs, REBOOTS, and verifies)\n"
        "  keys list        list every key on the device (read-only), marking\n"
        "                   protected device keys [PROTECTED] (needs a passphrase)\n"
        "  keys pub KID     print a key's public material and typed metadata\n"
        "                   (read-only; --hex / --raw select the encoding)\n"
        "  keys gen TYPE    generate a key of TYPE (e.g. ED25519, SECP256R1,\n"
        "                   AES256); needs --label; prints the new key id\n"
        "  keys rm          delete keys: --all (non-protected) or --label-prefix P;\n"
        "                   protected keys need an exact label + per-key 'YES'\n"
        "  sign KID         sign a message (stdin or --in FILE, max 2048 bytes)\n"
        "                   with the device key KID; prints the signature as\n"
        "                   base64 (--hex / --raw select the encoding)\n"
        "  random N         print N bytes (1..4096) of device hardware RNG as\n"
        "                   lowercase hex (--raw for binary); uses --kid KID's\n"
        "                   AES key, else a transient EHEMTEST key\n");
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

/* REQ-TOOL-003: harvest the cloud-delivered certificate and install it on a
 * device whose firmware cannot apply it itself, then reboot and verify. */
static int cmd_cert_install(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_cert_install_opts co;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&co, 0, sizeof co);
    co.passphrase    = o->passphrase;
    co.force         = o->force;
    co.insecure      = o->insecure;
    co.poll_attempts = HEM_CERT_DEFAULT_POLL_ATTEMPTS;
    co.poll_delay_ms = HEM_CERT_DEFAULT_POLL_DELAY_MS;   /* real wait between polls */
    co.out           = stdout;
    co.err           = stderr;

    ret = hem_cert_install_run(ctx, &co);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-004: read-only inventory of the device's keys, protected keys
 * marked. The listing/marking lives in hem-tool-core (shared with keys rm). */
static int cmd_keys_list(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_keys_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_list_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-006: delete keys with the protected-key guard; the selection,
 * partition, prompts, and deletion live in hem-tool-core (shared with tests). */
static int cmd_keys_rm(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_keys_rm_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase   = o->passphrase;
    ko.all          = o->all;
    ko.prefixes     = o->prefixes;
    ko.prefix_count = o->prefix_count;
    ko.dry_run      = o->dry_run;
    ko.assume_yes   = o->assume_yes;
    ko.out          = stdout;
    ko.err          = stderr;
    ko.in           = stdin;

    ret = hem_keys_rm_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-007: read-only public material + typed metadata for one key; the
 * fetch/format logic lives in hem-tool-core (shared with the unit test). */
static int cmd_keys_pub(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_keys_pub_opts ko;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.kid        = kid;
    ko.format     = o->raw ? HEM_KEYS_PUB_RAW
                           : (o->hex ? HEM_KEYS_PUB_HEX : HEM_KEYS_PUB_B64);
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_pub_run(ctx, &ko);
    if (ko.format != HEM_KEYS_PUB_RAW) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-008: sign a message with a device key; the input handling,
 * default-alg lookup, and formatting live in hem-tool-core. */
static int cmd_sign(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_sign_opts so;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&so, 0, sizeof so);
    so.passphrase = o->passphrase;
    so.kid        = kid;
    so.alg        = o->alg;
    so.in_path    = o->in_path;
    so.sigctx     = o->sigctx;
    so.format     = o->raw ? HEM_SIGN_OUT_RAW
                           : (o->hex ? HEM_SIGN_OUT_HEX : HEM_SIGN_OUT_B64);
    so.out        = stdout;
    so.err        = stderr;
    so.in         = stdin;

    ret = hem_sign_run(ctx, &so);
    if (so.format != HEM_SIGN_OUT_RAW) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-010: device hardware-RNG bytes; the count parsing, transient-key
 * orchestration, and formatting live in hem-tool-core. */
static int cmd_random(const cli_opts *o, const char *count_arg)
{
    ehem_ctx *ctx = NULL;
    hem_random_opts ro;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ro, 0, sizeof ro);
    ro.passphrase = o->passphrase;
    ro.count_arg  = count_arg;
    ro.kid        = o->kid;
    ro.raw        = o->raw;
    ro.out        = stdout;
    ro.err        = stderr;

    ret = hem_random_run(ctx, &ro);
    if (!ro.raw) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-009: generate a key on the device; the mode default and error
 * mapping live in hem-tool-core. `type` is the third positional (TYPE). */
static int cmd_keys_gen(const cli_opts *o, const char *type)
{
    ehem_ctx *ctx = NULL;
    hem_keys_gen_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.type       = type;
    ko.label      = o->label;
    ko.descr      = o->descr;
    ko.mode       = o->mode;
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_gen_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* Dispatch the `keys` command group (list / pub / gen / rm). */
static int cmd_keys(const cli_opts *o, const char *subcmd, const char *arg)
{
    if (subcmd == NULL) {
        fprintf(stderr, "error: 'keys' needs a subcommand (list, pub, gen, rm)\n");
        usage(stderr);
        return 2;
    }
    /* Only `pub` (KID) and `gen` (TYPE) take a third positional. */
    if (arg != NULL && strcmp(subcmd, "pub") != 0 && strcmp(subcmd, "gen") != 0) {
        fprintf(stderr, "error: unexpected argument '%s'\n", arg);
        usage(stderr);
        return 2;
    }
    if (strcmp(subcmd, "list") == 0) {
        return cmd_keys_list(o);
    }
    if (strcmp(subcmd, "pub") == 0) {
        return cmd_keys_pub(o, arg);
    }
    if (strcmp(subcmd, "gen") == 0) {
        return cmd_keys_gen(o, arg);
    }
    if (strcmp(subcmd, "rm") == 0) {
        return cmd_keys_rm(o);
    }
    fprintf(stderr, "error: unknown keys subcommand '%s'\n", subcmd);
    usage(stderr);
    return 2;
}

int main(int argc, char **argv)
{
    cli_opts o;
    const char *cmd = NULL;
    const char *subcmd = NULL;
    const char *arg = NULL;
    int i;
    int ret;

    memset(&o, 0, sizeof o);
    o.url = getenv("EHEM_URL");           /* --url overrides below */
    o.passphrase = getenv("EHEM_PASSPHRASE");  /* --passphrase overrides below */

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
        } else if (strcmp(a, "--passphrase") == 0 && i + 1 < argc) {
            o.passphrase = argv[++i];
        } else if (strncmp(a, "--passphrase=", 13) == 0) {
            o.passphrase = a + 13;
        } else if (strcmp(a, "--force") == 0) {
            o.force = 1;
        } else if (strcmp(a, "--all") == 0) {
            o.all = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(a, "--yes") == 0) {
            o.assume_yes = 1;
        } else if (strcmp(a, "--label-prefix") == 0 && i + 1 < argc) {
            if (o.prefix_count >= MAX_LABEL_PREFIXES) {
                fprintf(stderr, "error: too many --label-prefix (max %d)\n",
                        MAX_LABEL_PREFIXES);
                return 2;
            }
            o.prefixes[o.prefix_count++] = argv[++i];
        } else if (strncmp(a, "--label-prefix=", 15) == 0) {
            if (o.prefix_count >= MAX_LABEL_PREFIXES) {
                fprintf(stderr, "error: too many --label-prefix (max %d)\n",
                        MAX_LABEL_PREFIXES);
                return 2;
            }
            o.prefixes[o.prefix_count++] = a + 15;
        } else if (strcmp(a, "--alg") == 0 && i + 1 < argc) {
            o.alg = argv[++i];
        } else if (strncmp(a, "--alg=", 6) == 0) {
            o.alg = a + 6;
        } else if (strcmp(a, "--in") == 0 && i + 1 < argc) {
            o.in_path = argv[++i];
        } else if (strncmp(a, "--in=", 5) == 0) {
            o.in_path = a + 5;
        } else if (strcmp(a, "--sigctx") == 0 && i + 1 < argc) {
            o.sigctx = argv[++i];
        } else if (strncmp(a, "--sigctx=", 9) == 0) {
            o.sigctx = a + 9;
        } else if (strcmp(a, "--label") == 0 && i + 1 < argc) {
            o.label = argv[++i];
        } else if (strncmp(a, "--label=", 8) == 0) {
            o.label = a + 8;
        } else if (strcmp(a, "--descr") == 0 && i + 1 < argc) {
            o.descr = argv[++i];
        } else if (strncmp(a, "--descr=", 8) == 0) {
            o.descr = a + 8;
        } else if (strcmp(a, "--mode") == 0 && i + 1 < argc) {
            o.mode = argv[++i];
        } else if (strncmp(a, "--mode=", 7) == 0) {
            o.mode = a + 7;
        } else if (strcmp(a, "--kid") == 0 && i + 1 < argc) {
            o.kid = argv[++i];
        } else if (strncmp(a, "--kid=", 6) == 0) {
            o.kid = a + 6;
        } else if (strcmp(a, "--insecure") == 0) {
            o.insecure = 1;
        } else if (strcmp(a, "--hex") == 0) {
            o.hex = 1;
        } else if (strcmp(a, "--raw") == 0) {
            o.raw = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else if (cmd == NULL) {
            cmd = a;
        } else if (subcmd == NULL) {
            subcmd = a;                 /* e.g. the `list` in `keys list` */
        } else if (arg == NULL) {
            arg = a;                    /* e.g. the KID in `keys pub KID` */
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
    } else if (strcmp(cmd, "cert-install") == 0) {
        ret = cmd_cert_install(&o);
    } else if (strcmp(cmd, "keys") == 0) {
        ret = cmd_keys(&o, subcmd, arg);
    } else if (strcmp(cmd, "sign") == 0) {
        if (arg != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", arg);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_sign(&o, subcmd);   /* subcmd is the KID */
        }
    } else if (strcmp(cmd, "random") == 0) {
        if (arg != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", arg);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_random(&o, subcmd); /* subcmd is the byte count N */
        }
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        usage(stderr);
        ret = 2;
    }

    ehem_global_cleanup();
    return ret;
}
