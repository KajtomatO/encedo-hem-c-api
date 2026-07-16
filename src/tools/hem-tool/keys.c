/*
 * keys.c — hem-tool `keys` subcommands + the protected-key classifier.
 *
 * implements: REQ-TOOL-005, REQ-TOOL-004
 *
 * Public-API-only (include/ehem/), so the same code drives the real device from
 * main() and the fake transport from the unit test.
 */
#include "keys.h"

#include <ctype.h>
#include <string.h>

#include "ehem/auth.h"
#include "ehem/keymgmt.h"

/* -------------------------------------------------------------------------- */
/* protected-key classifier (REQ-TOOL-005)                                    */
/* -------------------------------------------------------------------------- */

/* Case-insensitive substring test: does `hay` contain `ndl_lower` (which is
 * already lowercase)? */
static bool ci_contains(const char *hay, const char *ndl_lower)
{
    size_t nlen = strlen(ndl_lower);
    size_t i;

    if (nlen == 0) {
        return true;
    }
    for (i = 0; hay[i] != '\0'; i++) {
        size_t k = 0;
        while (k < nlen && hay[i + k] != '\0' &&
               (char)tolower((unsigned char)hay[i + k]) == ndl_lower[k]) {
            k++;
        }
        if (k == nlen) {
            return true;
        }
    }
    return false;
}

bool hem_key_is_protected(const char *label)
{
    if (label == NULL) {
        return false;
    }
    /* Exact device TLS material. */
    if (strcmp(label, "TLS PrivateKey") == 0 ||
        strcmp(label, "TLS Certificate") == 0) {
        return true;
    }
    /* Paired phone authenticators (any case, any position). */
    return ci_contains(label, "(android)") || ci_contains(label, "(iphone)");
}

/* -------------------------------------------------------------------------- */
/* keys list (REQ-TOOL-004)                                                   */
/* -------------------------------------------------------------------------- */

/* Print the last-error detail recorded on the context. */
static void report(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(err, "  detail: %s\n", e->message);
        }
        if (e->http_status != 0) {
            fprintf(err, "  http status: %ld\n", e->http_status);
        }
        if (e->device_payload != NULL) {
            fprintf(err, "  device: %s\n", e->device_payload);
        }
    }
}

int hem_keys_list_run(ehem_ctx *ctx, const hem_keys_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_key_page *page = NULL;
    ehem_rc rc;
    size_t i;
    unsigned long protected_count = 0;

    if (o->passphrase == NULL) {
        fprintf(err, "error: no passphrase — pass --passphrase or set "
                     "EHEM_PASSPHRASE\n");
        return HEM_KEYS_USAGE;
    }

    /* Login is lazy (no traffic); the list call performs the auth exchange. */
    rc = ehem_login(ctx, o->passphrase);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "login");
        return HEM_KEYS_RUNTIME;
    }

    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys list");
        return HEM_KEYS_RUNTIME;
    }

    for (i = 0; i < page->listed; i++) {
        const ehem_key_entry *e = &page->entries[i];
        bool prot = hem_key_is_protected(e->label);
        if (prot) {
            protected_count++;
        }
        fprintf(out, "  %s  '%s'  (%s)%s\n",
                e->kid,
                (e->label != NULL) ? e->label : "",
                e->type,
                prot ? "  [PROTECTED]" : "");
    }
    fprintf(out, "\n%lu key(s), %lu protected\n",
            (unsigned long)page->listed, protected_count);

    ehem_key_page_free(page);
    return HEM_KEYS_OK;
}
