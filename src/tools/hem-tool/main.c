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
 *             REQ-TOOL-009 (the `keys gen` subcommand),
 *             REQ-TOOL-011 (the `keys update` subcommand),
 *             REQ-TOOL-012 (the `logs` subcommands),
 *             REQ-TOOL-013 (the `selftest` subcommand),
 *             REQ-TOOL-014 (the `reboot` subcommand),
 *             REQ-TOOL-015 (the `tls-recover` subcommand)
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
#include <time.h>

#include "ehem/auth.h"
#include "ehem/ehem.h"
#include "ehem/system.h"

#include "cert_install.h"
#include "ext_cmd.h"
#include "keys.h"
#include "logs.h"
#include "random.h"
#include "recover.h"
#include "registry.h"
#include "selftest.h"
#include "sign.h"
#include "tool_auth.h"

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

    /* logs get output (REQ-TOOL-012). */
    const char *out_path;     /* --out (write the log file here; NULL → stdout) */

    /* ext family (REQ-TOOL-016). */
    int         no_qr;        /* --no-qr (print the QR payload JSON instead) */
    const char *scope;        /* --scope (ext login; NULL → system:config) */
    const char *note;         /* --note (free text for the phone UI) */
    long        timeout_sec;  /* --timeout (ext login confirmation, seconds) */
    const char *notify_url;   /* --notify-url (broker base override) */

    /* reboot behavior (REQ-TOOL-014). */
    int         wait_back;    /* --wait: poll until the device answers again */

    /* auth mode (REQ-TOOL-018). */
    int         mobile;       /* --mobile: push-confirm instead of passphrase */
    int         pw_flag;      /* --passphrase given explicitly (vs env) —
                               * --mobile + --passphrase is a usage error */

    /* help (REQ-TOOL-020): -h/--help defers until the command is known so
     * `hem-tool keys rm --help` renders keys-rm help, not the top page. */
    int         want_help;
} cli_opts;

/* REQ-TOOL-019/020: the top-level page renders from the command
 * registry (single source of truth in hem-tool-core). */
static void usage(FILE *f)
{
    hem_help_top(f, ehem_version());
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

/* implements: REQ-TOOL-018 — every mobile push announces what it asks for
 * (one line per scope acquisition; multi-scope commands push more than once). */
static void mobile_push_notice(const char *scope, long timeout_ms, void *arg)
{
    (void)arg;
    fprintf(stderr, "mobile: push sent — approve \"%s\" on your phone "
                    "(waiting up to %ld s)\n", scope, timeout_ms / 1000);
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
    if (o->timeout_sec > 0) {
        /* implements: REQ-TOOL-016 (ext login --timeout → confirm wait) */
        opts.confirm_timeout_ms = o->timeout_sec * 1000L;
    }
    if (o->mobile) {
        /* ext login keeps its own richer push line (hook set only here). */
        opts.confirm_notice = mobile_push_notice;
    }
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
    co.mobile       = o->mobile;
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
    ko.mobile     = o->mobile;
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
    ko.mobile       = o->mobile;
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
    ko.mobile     = o->mobile;
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
    so.mobile     = o->mobile;
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
    ro.mobile     = o->mobile;
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
    ko.mobile     = o->mobile;
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

static int cmd_keys_update(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_keys_update_opts uo;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&uo, 0, sizeof uo);
    uo.passphrase = o->passphrase;
    uo.mobile     = o->mobile;
    uo.kid        = kid;
    uo.label      = o->label;
    uo.descr      = o->descr;
    uo.assume_yes = o->assume_yes;
    uo.out        = stdout;
    uo.err        = stderr;
    uo.in         = stdin;

    ret = hem_keys_update_run(ctx, &uo);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* Dispatch the `keys` command group (list / pub / gen / rm / update). */
static int cmd_keys(const cli_opts *o, const char *subcmd, const char *arg)
{
    if (subcmd == NULL) {
        fprintf(stderr,
                "error: 'keys' needs a subcommand (list, pub, gen, rm, update)\n");
        usage(stderr);
        return 2;
    }
    /* Only `pub` (KID), `gen` (TYPE) and `update` (KID) take a positional. */
    if (arg != NULL && strcmp(subcmd, "pub") != 0 &&
        strcmp(subcmd, "gen") != 0 && strcmp(subcmd, "update") != 0) {
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
    if (strcmp(subcmd, "update") == 0) {
        return cmd_keys_update(o, arg);
    }
    fprintf(stderr, "error: unknown keys subcommand '%s'\n", subcmd);
    usage(stderr);
    return 2;
}

/* Dispatch the `logs` command group (list / get / key) — REQ-TOOL-012. */
static int cmd_logs(const cli_opts *o, const char *subcmd, const char *arg)
{
    ehem_ctx *ctx = NULL;
    hem_logs_opts lo;
    int ret;

    if (subcmd == NULL) {
        fprintf(stderr, "error: 'logs' needs a subcommand (list, get, key)\n");
        usage(stderr);
        return 2;
    }
    if (arg != NULL && strcmp(subcmd, "get") != 0) {
        fprintf(stderr, "error: unexpected argument '%s'\n", arg);
        usage(stderr);
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&lo, 0, sizeof lo);
    lo.passphrase = o->passphrase;
    lo.mobile     = o->mobile;
    lo.out        = stdout;
    lo.err        = stderr;

    if (strcmp(subcmd, "list") == 0) {
        ret = hem_logs_list_run(ctx, &lo);
    } else if (strcmp(subcmd, "get") == 0) {
        ret = hem_logs_get_run(ctx, &lo, arg, o->out_path);
    } else if (strcmp(subcmd, "key") == 0) {
        ret = hem_logs_key_run(ctx, &lo);
    } else {
        fprintf(stderr, "error: unknown logs subcommand '%s'\n", subcmd);
        usage(stderr);
        ret = 2;
    }
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `reboot [--wait]` — REQ-TOOL-014 over the REQ-SYS-005 binding. With
 * --wait, poll status until the device answers again (bounded ~90 s; the
 * loop is paced by each probe's short connect timeout — no sleep needed). */
static int cmd_reboot(const cli_opts *o, int wait_back)
{
    ehem_ctx *ctx = NULL;
    ehem_rc rc;
    int ret;

    if (!o->mobile && (o->passphrase == NULL || o->passphrase[0] == '\0')) {
        fprintf(stderr, "error: no passphrase — pass --passphrase / set "
                        "EHEM_PASSPHRASE, or use --mobile\n");
        return 2;
    }
    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    rc = hem_tool_login(ctx, o->passphrase, o->mobile != 0, stderr);
    if (rc == EHEM_OK) {
        rc = ehem_system_reboot(ctx);
    }
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "reboot");
        ehem_ctx_destroy(ctx);
        return hem_tool_auth_exit(rc, stderr, 1);
    }
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    fprintf(stderr, "reboot accepted — the device restarts now\n");

    if (!wait_back) {
        fprintf(stderr, "it should answer again in ~30-60 s "
                        "(use --wait to block until then)\n");
        return 0;
    }

    /* Two phases, probes spaced ≥ 1 s by the SDK's own request pacing: first
     * wait for the OLD instance to stop answering (the firmware serves for
     * ~2 s after accepting the reboot), then wait for status to answer again. */
    {
        time_t start = time(NULL);
        time_t deadline = start + 180;
        int attempts = 0;
        int seen_down = 0;
        for (;;) {
            ehem_options opts;
            ehem_ctx *probe = NULL;
            ehem_status_info *st = NULL;
            int up = 0;
            ehem_options_init(&opts);
            if (o->insecure) {
                opts.tls_mode = EHEM_TLS_INSECURE;
            } else if (o->cacert != NULL) {
                opts.tls_mode = EHEM_TLS_CA_FILE;
                opts.ca_file  = o->cacert;
            }
            opts.connect_timeout_ms = 3000;
            opts.total_timeout_ms   = 4000;
            opts.request_pace_ms    = 1000;
            if (ehem_ctx_create(o->url, &opts, &probe) == EHEM_OK &&
                ehem_system_status(probe, &st) == EHEM_OK) {
                ehem_system_status_free(st);
                up = 1;
            }
            ehem_ctx_destroy(probe);
            if (up && seen_down) {
                fprintf(stderr, "device back after ~%ld s\n",
                        (long)(time(NULL) - start));
                return 0;
            }
            if (!up) {
                seen_down = 1;
            }
            attempts++;
            if (time(NULL) >= deadline || attempts >= 180) {
                fprintf(stderr, "error: device did not return within ~180 s\n");
                return 1;
            }
        }
    }
}

/* `tls-recover [--force]` — REQ-TOOL-015. */
/* `ext pair|list|login` (REQ-TOOL-016). */
static int cmd_ext(const cli_opts *o, const char *subcmd)
{
    ehem_ctx *ctx = NULL;
    int ret;

    if (subcmd == NULL) {
        fprintf(stderr, "error: ext needs a subcommand (pair | list | login)\n");
        usage(stderr);
        return 2;
    }
    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    if (strcmp(subcmd, "pair") == 0) {
        hem_ext_pair_opts po;
        memset(&po, 0, sizeof po);
        po.passphrase = o->passphrase;
        po.mobile     = o->mobile;
        po.notify_url = o->notify_url;
        po.no_qr      = o->no_qr;
        ret = hem_ext_pair_run(ctx, &po);
    } else if (strcmp(subcmd, "list") == 0) {
        ret = hem_ext_list_run(ctx, o->passphrase, o->mobile != 0, NULL, NULL);
    } else if (strcmp(subcmd, "login") == 0) {
        hem_ext_login_opts lo;
        memset(&lo, 0, sizeof lo);
        lo.scope      = o->scope;
        lo.note       = o->note;
        lo.passphrase = o->passphrase;
        lo.timeout_ms = (o->timeout_sec > 0) ? o->timeout_sec * 1000L : 0;
        ret = hem_ext_login_run(ctx, &lo);
    } else {
        fprintf(stderr, "error: unknown ext subcommand '%s'\n", subcmd);
        usage(stderr);
        ret = 2;
    }

    ehem_ctx_destroy(ctx);
    return ret;
}

static int cmd_tls_recover(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_recover_opts ro;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&ro, 0, sizeof ro);
    ro.passphrase   = o->passphrase;
    ro.mobile       = o->mobile;
    ro.force        = o->force;
    ro.poll_delay_ms = HEM_RECOVER_POLL_DELAY_MS;
    ro.out          = stdout;
    ro.err          = stderr;

    ret = hem_tls_recover_run(ctx, &ro);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `selftest` — REQ-TOOL-013. */
static int cmd_selftest(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_selftest_opts so;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&so, 0, sizeof so);
    so.passphrase = o->passphrase;
    so.mobile     = o->mobile;
    so.out        = stdout;
    so.err        = stderr;

    ret = hem_selftest_run(ctx, &so);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
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
    /* o.url stays flag-only; env + default resolve AFTER parsing
     * (hem_tool_resolve_url, REQ-TOOL-017). */
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
            o.pw_flag = 1;
        } else if (strncmp(a, "--passphrase=", 13) == 0) {
            o.passphrase = a + 13;
            o.pw_flag = 1;
        } else if (strcmp(a, "--mobile") == 0) {
            o.mobile = 1;
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
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            o.out_path = argv[++i];
        } else if (strncmp(a, "--out=", 6) == 0) {
            o.out_path = a + 6;
        } else if (strcmp(a, "--no-qr") == 0) {
            o.no_qr = 1;
        } else if (strcmp(a, "--scope") == 0 && i + 1 < argc) {
            o.scope = argv[++i];
        } else if (strncmp(a, "--scope=", 8) == 0) {
            o.scope = a + 8;
        } else if (strcmp(a, "--note") == 0 && i + 1 < argc) {
            o.note = argv[++i];
        } else if (strncmp(a, "--note=", 7) == 0) {
            o.note = a + 7;
        } else if (strcmp(a, "--timeout") == 0 && i + 1 < argc) {
            o.timeout_sec = atol(argv[++i]);
        } else if (strncmp(a, "--timeout=", 10) == 0) {
            o.timeout_sec = atol(a + 10);
        } else if (strcmp(a, "--notify-url") == 0 && i + 1 < argc) {
            o.notify_url = argv[++i];
        } else if (strncmp(a, "--notify-url=", 13) == 0) {
            o.notify_url = a + 13;
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
        } else if (strcmp(a, "--wait") == 0) {
            o.wait_back = 1;
        } else if (strcmp(a, "--insecure") == 0) {
            o.insecure = 1;
        } else if (strcmp(a, "--hex") == 0) {
            o.hex = 1;
        } else if (strcmp(a, "--raw") == 0) {
            o.raw = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            o.want_help = 1;   /* deferred: per-command help needs the command */
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
    if (o.mobile && o.pw_flag) {
        /* implements: REQ-TOOL-018 — the explicit flags conflict; a
         * passphrase from the ENVIRONMENT is simply outranked by --mobile
         * (hem.env is commonly auto-sourced). */
        fprintf(stderr,
                "error: --mobile and --passphrase are mutually exclusive\n");
        return 2;
    }

    /* REQ-TOOL-020: `hem-tool help [<command> [<sub>]]` and the deferred
     * `<command> --help` spelling — both render from the registry. */
    if (cmd != NULL && strcmp(cmd, "help") == 0) {
        if (subcmd == NULL) {
            usage(stdout);
            return 0;
        }
        if (hem_help_command(stdout, subcmd, arg) != 0) {
            fprintf(stderr, "error: unknown command '%s%s%s'\n",
                    subcmd, arg != NULL ? " " : "", arg != NULL ? arg : "");
            return 2;
        }
        return 0;
    }
    if (o.want_help) {
        if (cmd == NULL) {
            usage(stdout);
            return 0;
        }
        if (hem_help_command(stdout, cmd, subcmd) != 0) {
            fprintf(stderr, "error: unknown command '%s%s%s'\n",
                    cmd, subcmd != NULL ? " " : "",
                    subcmd != NULL ? subcmd : "");
            return 2;
        }
        return 0;
    }

    if (cmd == NULL) {
        usage(stderr);
        return 2;
    }

    /* REQ-TOOL-017: --url > EHEM_URL > the built-in default (with a
     * one-line stderr notice when the default is used). */
    o.url = hem_tool_resolve_url(o.url, getenv("EHEM_URL"), stderr);

    if (strcmp(cmd, "status") == 0) {
        ret = cmd_status(&o);
    } else if (strcmp(cmd, "checkin") == 0) {
        ret = cmd_checkin(&o);
    } else if (strcmp(cmd, "cert-install") == 0) {
        ret = cmd_cert_install(&o);
    } else if (strcmp(cmd, "keys") == 0) {
        ret = cmd_keys(&o, subcmd, arg);
    } else if (strcmp(cmd, "logs") == 0) {
        ret = cmd_logs(&o, subcmd, arg);
    } else if (strcmp(cmd, "ext") == 0) {
        ret = cmd_ext(&o, subcmd);
    } else if (strcmp(cmd, "reboot") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_reboot(&o, o.wait_back);
        }
    } else if (strcmp(cmd, "tls-recover") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_tls_recover(&o);
        }
    } else if (strcmp(cmd, "selftest") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_selftest(&o);
        }
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
