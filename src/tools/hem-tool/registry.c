/*
 * registry.c — the hem-tool command registry + help rendering + URL
 * resolution (see registry.h).
 *
 * implements: REQ-TOOL-017, REQ-TOOL-019, REQ-TOOL-020
 */
#include "registry.h"

#include <string.h>

/* --- per-command option tables -------------------------------------------- */

static const hem_cmd_option OPT_CERT_INSTALL[] = {
    { "--force",          "install even when the device already serves the "
                          "delivered certificate" },
};
static const hem_cmd_option OPT_KEYS_PUB[] = {
    { "--hex",            "print the material as lowercase hex" },
    { "--raw",            "ONLY raw bytes on stdout (pipe-friendly)" },
};
static const hem_cmd_option OPT_KEYS_GEN[] = {
    { "--label LABEL",    "the key label (required, 1..32 printable ASCII)" },
    { "--descr STR",      "opaque description blob (raw bytes, max 64)" },
    { "--mode MODE",      "NIST-P/K only: ECDH | ExDSA | ECDH,ExDSA "
                          "(default ECDH,ExDSA so the key can sign)" },
};
static const hem_cmd_option OPT_KEYS_RM[] = {
    { "--all",            "target every non-protected key" },
    { "--label-prefix P", "target keys whose label starts with P (repeatable; "
                          "protected keys need the exact label)" },
    { "--dry-run",        "show what would be deleted, delete nothing" },
    { "--yes",            "skip the bulk prompt (never for protected keys)" },
};
static const hem_cmd_option OPT_KEYS_UPDATE[] = {
    { "--label LABEL",    "the new label (required)" },
    { "--descr STR",      "new descr; omitted keeps the stored one, "
                          "\"\" clears it (the device rewrites the whole "
                          "record)" },
    { "--yes",            "skip prompts (ignored for protected keys)" },
};
static const hem_cmd_option OPT_LOGS_GET[] = {
    { "--out FILE",       "write the log file here (default: stdout)" },
};
static const hem_cmd_option OPT_SIGN[] = {
    { "--alg ALG",        "algorithm selector (e.g. Ed25519, "
                          "SHA256WithECDSA); omitted → derived from the "
                          "key type" },
    { "--in FILE",        "read the message from FILE (default: stdin; "
                          "max 2048 bytes)" },
    { "--sigctx STR",     "RFC 8032 context for the Ed*ctx/Ed*ph selectors" },
    { "--hex",            "print the signature as lowercase hex" },
    { "--raw",            "ONLY raw signature bytes on stdout" },
};
static const hem_cmd_option OPT_RANDOM[] = {
    { "--kid KID",        "use this existing AES key (else a transient "
                          "EHEMTEST key is created and removed)" },
    { "--raw",            "ONLY raw bytes on stdout (default: lowercase hex)" },
};
static const hem_cmd_option OPT_EXT_PAIR[] = {
    { "--no-qr",          "print the QR payload JSON instead of the "
                          "terminal QR" },
    { "--notify-url URL", "notification-broker base override" },
};
static const hem_cmd_option OPT_EXT_LOGIN[] = {
    { "--scope S",        "scope to confirm (default system:config)" },
    { "--note STR",       "free text shown in the phone's push UI" },
    { "--timeout SEC",    "wait this long for the answer (default 60)" },
    { "--notify-url URL", "notification-broker base override" },
};
static const hem_cmd_option OPT_REBOOT[] = {
    { "--wait",           "block until the device answers again (~180 s max)" },
};
static const hem_cmd_option OPT_TLS_RECOVER[] = {
    { "--force",          "recover even when the device reports HTTPS up" },
};

/* --- the command table ---------------------------------------------------- */

#define N(a) a, (sizeof(a) / sizeof((a)[0]))

static const hem_command COMMANDS[] = {
    { NULL, "status", "status",
      "print device status and version",
      "Prints reachability, uptime, temperature, storage state, and the\n"
      "hardware/firmware/bootloader identity. The connection smoke test.",
      HEM_AUTH_NONE, NULL, 0 },

    { NULL, "checkin", "checkin",
      "check-in handshake (refreshes device TLS cert + clock)",
      "Runs the 3-leg device -> Encedo-cloud -> device check-in relay. As\n"
      "firmware side effects this resynchronizes the device clock (the RTC\n"
      "drifts, see KNOWN-ISSUES) and refreshes an expiring TLS certificate.",
      HEM_AUTH_NONE, NULL, 0 },

    { NULL, "cert-install", "cert-install [--force]",
      "harvest + install the cloud TLS certificate (REBOOTS)",
      "Harvests the cloud-delivered certificate from the check-in exchange,\n"
      "skips if the device already serves it, else authenticates, installs\n"
      "it, REBOOTS the device, and verifies. Distinct exit codes per\n"
      "failure mode; runs credential-less up to the 'already current' check.",
      HEM_AUTH_BEARER, N(OPT_CERT_INSTALL) },

    { "keys", "list", "keys list",
      "list every key, protected device keys marked [PROTECTED]",
      "Walks the whole key repository (read-only) and prints each key as\n"
      "kid, label, and type flags; device TLS material and paired phones\n"
      "are marked [PROTECTED] (the client-side label policy).",
      HEM_AUTH_BEARER, NULL, 0 },

    { "keys", "pub", "keys pub KID [--hex | --raw]",
      "print a key's public material and typed metadata",
      "Fetches one key by its 32-hex-char KID (read-only) and prints the\n"
      "public material (base64 by default) plus the classified type\n"
      "metadata. Symmetric keys have no public material (still exit 0).",
      HEM_AUTH_BEARER, N(OPT_KEYS_PUB) },

    { "keys", "gen", "keys gen TYPE --label LABEL [--descr STR] [--mode MODE]",
      "generate a key of TYPE on the device",
      "Generates a key (e.g. ED25519, SECP256R1, AES256 — the device\n"
      "validates the type) and prints the new key id. For NIST-P/K curves\n"
      "the tool defaults --mode to ECDH,ExDSA so the key can sign (the\n"
      "device's own default is ECDH-only).",
      HEM_AUTH_BEARER, N(OPT_KEYS_GEN) },

    { "keys", "rm", "keys rm (--all | --label-prefix P)... [--dry-run] [--yes]",
      "delete keys, protected keys guarded by an exact-label YES ritual",
      "Deletes the selected keys. Bulk selection NEVER touches protected\n"
      "keys (TLS material, paired phones); removing one requires naming its\n"
      "exact label and typing the literal YES at a per-key prompt — --yes\n"
      "is deliberately ignored for them.",
      HEM_AUTH_BEARER, N(OPT_KEYS_RM) },

    { "keys", "update", "keys update KID --label LABEL [--descr STR] [--yes]",
      "rewrite a key's label/descr (protected keys guarded)",
      "Rewrites a key's metadata. The device replaces the WHOLE record, so\n"
      "the tool re-sends the stored descr when --descr is omitted (\"\"\n"
      "clears it). Renaming a PROTECTED key needs the per-key YES ritual.",
      HEM_AUTH_BEARER, N(OPT_KEYS_UPDATE) },

    { "logs", "list", "logs list",
      "list audit-log file ids (one per line)",
      "Lists the device audit-log files (PPA builds; an EPA device answers\n"
      "404 / not found).",
      HEM_AUTH_BEARER, NULL, 0 },

    { "logs", "get", "logs get ID [--out FILE]",
      "download one audit-log file",
      "Downloads one audit-log file verbatim (binary-safe with --out; the\n"
      "on-wire format is the firmware's pipe-delimited record lines).",
      HEM_AUTH_BEARER, N(OPT_LOGS_GET) },

    { "logs", "key", "logs key",
      "print the Ed25519 log-signing key + signed nonce",
      "Prints the device's log-signing public key together with a freshly\n"
      "signed nonce proving the device holds the private half.",
      HEM_AUTH_BEARER, NULL, 0 },

    { NULL, "selftest", "selftest",
      "run the device self-test battery",
      "Runs the synchronous self-test battery (~5 s) and prints the result\n"
      "plus key-repository statistics. Exit 0 = healthy, 3 = the device\n"
      "reports a fail state.",
      HEM_AUTH_BEARER, NULL, 0 },

    { "ext", "pair", "ext pair [--no-qr] [--notify-url URL]",
      "pair the Encedo mobile app (terminal QR)",
      "Pairs the Encedo phone app: renders the pairing QR in the terminal\n"
      "(--no-qr prints the payload JSON), waits for the scan, and completes\n"
      "the registration. Passphrase-ONLY: the device demands sub=\"U\" for\n"
      "pairing changes, so --mobile is rejected here.",
      HEM_AUTH_PASSPHRASE_ONLY, N(OPT_EXT_PAIR) },

    { "ext", "list", "ext list",
      "list paired authenticators",
      "Lists the paired phone authenticators (EXTAID descriptors) with\n"
      "their pids; real phones' labels land in the protected-key policy.",
      HEM_AUTH_BEARER, NULL, 0 },

    { "ext", "login", "ext login [--scope S] [--timeout SEC] [--note STR]",
      "demo of the push-confirm login",
      "Sends a push to the paired phone and blocks for the answer. Exit 0\n"
      "approved, 3 timeout, 4 rejected, 5 nothing paired (this command's\n"
      "historical codes; every OTHER command under --mobile uses the\n"
      "tool-wide 13 = timeout / 14 = rejected).",
      HEM_AUTH_BEARER, N(OPT_EXT_LOGIN) },

    { NULL, "reboot", "reboot [--wait]",
      "reboot the device (DISRUPTIVE)",
      "Reboots the device — DISRUPTIVE: interrupts every user of it. With\n"
      "--wait, polls until the device answers again (two-phase: first seen\n"
      "down, then back; ~180 s bound).",
      HEM_AUTH_BEARER, N(OPT_REBOOT) },

    { NULL, "tls-recover", "tls-recover [--force]",
      "restore HTTPS after a device wipe (DISRUPTIVE)",
      "Restores HTTPS on a device that lost its TLS material: fetches a\n"
      "key+cert bundle from the provisioning cloud, installs it, REBOOTS,\n"
      "and polls until HTTPS is back. Run against the device's http:// URL.",
      HEM_AUTH_BEARER, N(OPT_TLS_RECOVER) },

    { NULL, "sign", "sign KID [--alg ALG] [--in FILE] [--sigctx STR] [--hex | --raw]",
      "sign a message with a device key",
      "Signs a message (stdin or --in FILE, max 2048 bytes) with the device\n"
      "key KID and prints the signature (base64 by default). The device\n"
      "hashes internally; --alg picks the selector, else the key's type\n"
      "decides.",
      HEM_AUTH_BEARER, N(OPT_SIGN) },

    { NULL, "random", "random N [--kid KID] [--raw]",
      "read N bytes (1..4096) of device hardware RNG",
      "Reads device hardware-RNG bytes (harvested from AES-CBC IVs —\n"
      "REQ-OPS-002) as lowercase hex, or raw with --raw. Uses --kid's AES\n"
      "key, else creates and removes a transient EHEMTEST key.",
      HEM_AUTH_BEARER, N(OPT_RANDOM) },
};

#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

/* Shared connection/auth options — rendered in top-level help and pointed
 * at from per-command help. --url documents the REQ-TOOL-017 default. */
static const hem_cmd_option SHARED_OPTIONS[] = {
    { "--url URL",        "device base URL (or EHEM_URL; default "
                          HEM_TOOL_DEFAULT_URL ")" },
    { "--cacert FILE",    "verify TLS against this CA/pinned certificate" },
    { "--insecure",       "skip TLS verification (lab use only)" },
    { "--passphrase PW",  "login passphrase (or EHEM_PASSPHRASE)" },
    { "--mobile",         "push confirmation on the paired phone instead of "
                          "a passphrase (exit 13 = no answer, 14 = rejected)" },
    { "--timeout SEC",    "mobile confirmation wait (default 60)" },
};
#define SHARED_OPTION_COUNT (sizeof(SHARED_OPTIONS) / sizeof(SHARED_OPTIONS[0]))

/* --- lookups -------------------------------------------------------------- */

const hem_command *hem_registry_commands(size_t *count)
{
    if (count != NULL) {
        *count = COMMAND_COUNT;
    }
    return COMMANDS;
}

static int is_family(const char *name)
{
    size_t i;
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (COMMANDS[i].family != NULL &&
            strcmp(COMMANDS[i].family, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const hem_command *hem_registry_find(const char *cmd, const char *subcmd)
{
    size_t i;
    if (cmd == NULL) {
        return NULL;
    }
    for (i = 0; i < COMMAND_COUNT; i++) {
        const hem_command *c = &COMMANDS[i];
        if (c->family == NULL) {
            if (subcmd == NULL && strcmp(c->name, cmd) == 0) {
                return c;
            }
        } else if (subcmd != NULL && strcmp(c->family, cmd) == 0 &&
                   strcmp(c->name, subcmd) == 0) {
            return c;
        }
    }
    return NULL;
}

const char *hem_auth_class_str(hem_auth_class c)
{
    switch (c) {
    case HEM_AUTH_NONE:            return "none";
    case HEM_AUTH_BEARER:          return "any bearer (passphrase or --mobile)";
    case HEM_AUTH_PASSPHRASE_ONLY: return "passphrase only";
    default:                       return "?";
    }
}

/* --- rendering ------------------------------------------------------------ */

static void print_options(FILE *f, const hem_cmd_option *opts, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        fprintf(f, "  %-18s %s\n", opts[i].flag, opts[i].help);
    }
}

static void print_group(FILE *f, hem_auth_class cls, const char *header)
{
    size_t i;
    fprintf(f, "%s\n", header);
    for (i = 0; i < COMMAND_COUNT; i++) {
        const hem_command *c = &COMMANDS[i];
        char full[32];
        if (c->auth != cls) {
            continue;
        }
        if (c->family != NULL) {
            snprintf(full, sizeof full, "%s %s", c->family, c->name);
        } else {
            snprintf(full, sizeof full, "%s", c->name);
        }
        fprintf(f, "  %-16s %s\n", full, c->summary);
    }
}

void hem_help_top(FILE *f, const char *version)
{
    fprintf(f, "hem-tool %s — Encedo HEM device CLI\n"
               "usage: hem-tool [options] <command> [command options]\n"
               "       hem-tool help <command>   (or: <command> --help)\n\n",
            version != NULL ? version : "");
    fprintf(f, "connection/auth options (every command):\n");
    print_options(f, SHARED_OPTIONS, SHARED_OPTION_COUNT);
    fputc('\n', f);
    print_group(f, HEM_AUTH_NONE,
                "commands needing NO credentials:");
    fputc('\n', f);
    print_group(f, HEM_AUTH_BEARER,
                "commands needing a bearer — passphrase or --mobile:");
    fputc('\n', f);
    print_group(f, HEM_AUTH_PASSPHRASE_ONLY,
                "commands needing a PASSPHRASE (the device demands sub=\"U\"):");
    fprintf(f, "\ncommand options: `hem-tool help <command>`, e.g. "
               "`hem-tool help keys rm`\n");
}

static void print_command_help(FILE *f, const hem_command *c)
{
    if (c->family != NULL) {
        fprintf(f, "hem-tool %s %s — %s\n\n", c->family, c->name, c->summary);
    } else {
        fprintf(f, "hem-tool %s — %s\n\n", c->name, c->summary);
    }
    fprintf(f, "usage: hem-tool [options] %s\n", c->synopsis);
    fprintf(f, "auth:  %s\n\n", hem_auth_class_str(c->auth));
    fprintf(f, "%s\n", c->details);
    if (c->option_count > 0) {
        fprintf(f, "\noptions:\n");
        print_options(f, c->options, c->option_count);
    }
    fprintf(f, "\nconnection/auth options are shared — see `hem-tool --help`\n");
}

static void print_family_help(FILE *f, const char *family)
{
    size_t i;
    fprintf(f, "hem-tool %s — subcommands:\n\n", family);
    for (i = 0; i < COMMAND_COUNT; i++) {
        const hem_command *c = &COMMANDS[i];
        if (c->family != NULL && strcmp(c->family, family) == 0) {
            fprintf(f, "  %s %-10s %s\n", c->family, c->name, c->summary);
        }
    }
    fprintf(f, "\ndetails: `hem-tool help %s <subcommand>`\n", family);
}

int hem_help_command(FILE *f, const char *cmd, const char *subcmd)
{
    const hem_command *c;
    if (cmd == NULL) {
        return -1;
    }
    c = hem_registry_find(cmd, subcmd);
    if (c != NULL) {
        print_command_help(f, c);
        return 0;
    }
    if (subcmd == NULL && is_family(cmd)) {
        print_family_help(f, cmd);
        return 0;
    }
    return -1;
}

/* --- REQ-TOOL-017: device URL resolution ---------------------------------- */

const char *hem_tool_resolve_url(const char *flag_url, const char *env_url,
                                 FILE *err)
{
    if (flag_url != NULL && flag_url[0] != '\0') {
        return flag_url;
    }
    if (env_url != NULL && env_url[0] != '\0') {
        return env_url;
    }
    fprintf(err != NULL ? err : stderr,
            "notice: no --url/EHEM_URL — using the default device URL "
            HEM_TOOL_DEFAULT_URL "\n");
    return HEM_TOOL_DEFAULT_URL;
}
