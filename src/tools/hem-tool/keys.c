/*
 * keys.c — hem-tool `keys` subcommands + the protected-key classifier.
 *
 * implements: REQ-TOOL-004, REQ-TOOL-005, REQ-TOOL-006
 *
 * Public-API-only (include/ehem/), so the same code drives the real device from
 * main() and the fake transport from the unit test.
 */
#include "keys.h"

#include <ctype.h>
#include <stdlib.h>
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

/* Print one key line: "<indent><kid>  '<label>'  (<type>)<suffix>". */
static void fprint_key(FILE *f, const char *indent, const ehem_key_entry *e,
                       const char *suffix)
{
    fprintf(f, "%s%s  '%s'  (%s)%s\n",
            indent, e->kid, (e->label != NULL) ? e->label : "", e->type,
            (suffix != NULL) ? suffix : "");
}

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
        fprint_key(out, "  ", e, prot ? "  [PROTECTED]" : "");
    }
    fprintf(out, "\n%lu key(s), %lu protected\n",
            (unsigned long)page->listed, protected_count);

    ehem_key_page_free(page);
    return HEM_KEYS_OK;
}

/* -------------------------------------------------------------------------- */
/* keys pub (REQ-TOOL-007)                                                    */
/* -------------------------------------------------------------------------- */

/* keys.h tags the whole file; the pub-specific point is here.
 * implements: REQ-TOOL-007 */

/* True iff `s` is exactly 32 hex chars (a wire-format kid). Tool-side copy —
 * hem-tool-core is public-API-only, so it cannot borrow the SDK's internal
 * validator; the SDK re-validates anyway (defense in depth). */
static bool kid_ok(const char *s)
{
    size_t i;
    if (s == NULL) {
        return false;
    }
    for (i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return s[32] == '\0';
}

/* Emit `raw` to `f` as padded std base64 (RFC 4648). Local, dependency-free —
 * the public SDK API hands back decoded bytes and offers no encoder. */
static void fprint_b64(FILE *f, const uint8_t *raw, size_t len)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    for (i = 0; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8) |
                     raw[i + 2];
        fprintf(f, "%c%c%c%c", T[(v >> 18) & 63], T[(v >> 12) & 63],
                T[(v >> 6) & 63], T[v & 63]);
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)raw[i] << 16;
        fprintf(f, "%c%c==", T[(v >> 18) & 63], T[(v >> 12) & 63]);
    } else if (len - i == 2) {
        uint32_t v = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8);
        fprintf(f, "%c%c%c=", T[(v >> 18) & 63], T[(v >> 12) & 63],
                T[(v >> 6) & 63]);
    }
}

static void fprint_hex(FILE *f, const uint8_t *raw, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        fprintf(f, "%02x", raw[i]);
    }
}

int hem_keys_pub_run(ehem_ctx *ctx, const hem_keys_pub_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_key_details *d = NULL;
    ehem_key_type_info info;
    const uint8_t *material;
    size_t material_len;
    const char *material_name;
    ehem_rc rc;

    if (o->passphrase == NULL) {
        fprintf(err, "error: no passphrase — pass --passphrase or set "
                     "EHEM_PASSPHRASE\n");
        return HEM_KEYS_USAGE;
    }
    if (!kid_ok(o->kid)) {
        fprintf(err, "error: 'keys pub' needs a key id "
                     "(exactly 32 hex chars)\n");
        return HEM_KEYS_USAGE;
    }

    rc = ehem_login(ctx, o->passphrase);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "login");
        return HEM_KEYS_RUNTIME;
    }

    rc = ehem_key_get(ctx, o->kid, &d);
    if (rc == EHEM_ERR_NOT_FOUND) {
        fprintf(err, "error: key not found: %s\n", o->kid);
        return HEM_KEYS_RUNTIME;
    }
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys pub");
        return HEM_KEYS_RUNTIME;
    }

    if (d->pubkey != NULL) {
        material = d->pubkey;
        material_len = d->pubkey_len;
        material_name = "pubkey";
    } else if (d->der != NULL) {
        material = d->der;
        material_len = d->der_len;
        material_name = "der";
    } else {
        material = NULL;
        material_len = 0;
        material_name = NULL;
    }

    if (o->format == HEM_KEYS_PUB_RAW) {
        /* Pipeline mode: o->out carries the material bytes and NOTHING else. */
        if (material != NULL) {
            fwrite(material, 1, material_len, out);
        } else {
            fprintf(err, "note: %s has no public material (symmetric key)\n",
                    o->kid);
        }
        ehem_key_details_free(d);
        return HEM_KEYS_OK;
    }

    (void)ehem_key_type_parse(d->type, &info);    /* NULLs already excluded */
    fprintf(out, "kid:      %s\n", o->kid);
    fprintf(out, "type:     %s\n", d->type);
    fprintf(out, "family:   %s\n", ehem_key_family_str(info.family));
    if (info.modes != 0) {
        fprintf(out, "modes:    %s%s%s\n",
                (info.modes & EHEM_KEY_MODE_EXDSA) ? "ExDSA" : "",
                (info.modes == (EHEM_KEY_MODE_EXDSA | EHEM_KEY_MODE_ECDH))
                    ? "," : "",
                (info.modes & EHEM_KEY_MODE_ECDH) ? "ECDH" : "");
    }
    if (d->updated != 0) {
        fprintf(out, "updated:  %lld\n", (long long)d->updated);
    }
    if (material != NULL) {
        fprintf(out, "%s:%s", material_name,
                strcmp(material_name, "der") == 0 ? "      " : "   ");
        if (o->format == HEM_KEYS_PUB_HEX) {
            fprint_hex(out, material, material_len);
        } else {
            fprint_b64(out, material, material_len);
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "material: (none — symmetric key exports no public "
                     "material)\n");
    }

    ehem_key_details_free(d);
    return HEM_KEYS_OK;
}

/* -------------------------------------------------------------------------- */
/* keys rm (REQ-TOOL-006)                                                     */
/* -------------------------------------------------------------------------- */

static bool startswith(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Read one line from `f` into `buf` (cap), stripping the trailing newline.
 * Returns false on EOF with no line read. */
static bool read_line(FILE *f, char *buf, size_t cap)
{
    size_t n;
    if (fgets(buf, (int)cap, f) == NULL) {
        buf[0] = '\0';
        return false;
    }
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return true;
}

/* Trim surrounding whitespace and lowercase in place. */
static void trim_lower(char *s)
{
    char *p = s;
    size_t n;
    size_t i;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    for (i = 0; s[i] != '\0'; i++) {
        s[i] = (char)tolower((unsigned char)s[i]);
    }
}

/* Bulk prompt for the regular batch: only a trimmed/lowercased "y" proceeds. */
static bool confirm_bulk(FILE *in, FILE *out, size_t count)
{
    char line[64];
    fprintf(out, "\ndelete %lu regular key(s)? [y/N] ", (unsigned long)count);
    fflush(out);
    if (!read_line(in, line, sizeof line)) {
        return false;                    /* EOF → no */
    }
    trim_lower(line);
    return strcmp(line, "y") == 0;
}

/* Per-key protected prompt: ONLY the literal uppercase "YES" proceeds. */
static bool confirm_protected(FILE *in, FILE *out, const ehem_key_entry *e)
{
    char line[64];
    fprintf(out,
            "\nABOUT TO DELETE PROTECTED DEVICE KEY:\n"
            "  kid:   %s\n  label: '%s'\n  type:  %s\n"
            "This may render the device unreachable or break phone pairing.\n"
            "type 'YES' (uppercase) to confirm, anything else aborts: ",
            e->kid, (e->label != NULL) ? e->label : "", e->type);
    fflush(out);
    if (!read_line(in, line, sizeof line)) {
        return false;                    /* EOF → abort */
    }
    return strcmp(line, "YES") == 0;
}

int hem_keys_rm_run(ehem_ctx *ctx, const hem_keys_rm_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    FILE *in  = (o->in  != NULL) ? o->in  : stdin;
    ehem_key_page *page = NULL;
    const ehem_key_entry **regular = NULL;
    const ehem_key_entry **prot_exact = NULL;
    const ehem_key_entry **prot_partial = NULL;
    size_t nreg = 0, nexact = 0, npartial = 0;
    size_t reg_fail = 0, prot_fail = 0, prot_skip = 0;
    size_t n, i;
    ehem_rc rc;
    int exit_code = HEM_KEYS_OK;

    /* Selection: exactly one of --all / --label-prefix. */
    if (o->all && o->prefix_count > 0) {
        fprintf(err, "error: --all and --label-prefix are mutually exclusive\n");
        return HEM_KEYS_USAGE;
    }
    if (!o->all && o->prefix_count == 0) {
        fprintf(err, "error: keys rm needs --all or one or more "
                     "--label-prefix PREFIX\n");
        return HEM_KEYS_USAGE;
    }
    if (o->passphrase == NULL) {
        fprintf(err, "error: no passphrase — pass --passphrase or set "
                     "EHEM_PASSPHRASE\n");
        return HEM_KEYS_USAGE;
    }

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

    n = page->listed;
    if (n > 0) {
        regular      = malloc(n * sizeof *regular);
        prot_exact   = malloc(n * sizeof *prot_exact);
        prot_partial = malloc(n * sizeof *prot_partial);
        if (regular == NULL || prot_exact == NULL || prot_partial == NULL) {
            free(regular);
            free(prot_exact);
            free(prot_partial);
            ehem_key_page_free(page);
            fprintf(err, "error: out of memory\n");
            return HEM_KEYS_RUNTIME;
        }
    }

    /* Partition (REQ-TOOL-006 §1/§4). A protected key is a target only when a
     * prefix equals its label EXACTLY; a prefix-only hit is warned + skipped.
     * Under --all, protected keys are silently excluded. */
    for (i = 0; i < n; i++) {
        const ehem_key_entry *e = &page->entries[i];
        bool prot = hem_key_is_protected(e->label);
        if (o->all) {
            if (!prot) {
                regular[nreg++] = e;
            }
        } else {
            bool hit = false, exact = false;
            size_t k;
            if (e->label != NULL) {
                for (k = 0; k < o->prefix_count; k++) {
                    if (startswith(e->label, o->prefixes[k])) {
                        hit = true;
                        if (strcmp(e->label, o->prefixes[k]) == 0) {
                            exact = true;
                        }
                    }
                }
            }
            if (!hit) {
                continue;
            }
            if (!prot) {
                regular[nreg++] = e;
            } else if (exact) {
                prot_exact[nexact++] = e;
            } else {
                prot_partial[npartial++] = e;
            }
        }
    }

    /* Partition report (REQ-TOOL-006 §2). */
    fprintf(out, "%lu key(s) total — ", (unsigned long)n);
    if (o->all) {
        fprintf(out, "ALL keys (excluding protected device keys)\n");
    } else {
        fprintf(out, "keys matching label prefix(es):");
        for (i = 0; i < o->prefix_count; i++) {
            fprintf(out, " '%s'", o->prefixes[i]);
        }
        fprintf(out, "\n");
    }
    fprintf(out,
            "  regular targets:   %lu\n"
            "  protected targets: %lu  (require per-key confirmation)\n"
            "  protected skipped: %lu  (partial-match only)\n",
            (unsigned long)nreg, (unsigned long)nexact, (unsigned long)npartial);

    if (npartial > 0) {
        fprintf(out,
                "\nWARNING: these PROTECTED keys partially match a prefix and are\n"
                "SKIPPED. To remove one, use --label-prefix with its EXACT label:\n");
        for (i = 0; i < npartial; i++) {
            fprint_key(out, "  ! ", prot_partial[i], "");
        }
    }
    if (nreg > 0) {
        fprintf(out, "\nregular keys to delete:\n");
        for (i = 0; i < nreg; i++) {
            fprint_key(out, "  ", regular[i], "");
        }
    }
    if (nexact > 0) {
        fprintf(out, "\nPROTECTED device keys queued for deletion "
                     "(per-key confirmation):\n");
        for (i = 0; i < nexact; i++) {
            fprint_key(out, "  * ", prot_exact[i], "");
        }
    }

    if (nreg == 0 && nexact == 0) {
        fprintf(out, "\nnothing to do\n");
        goto cleanup;
    }
    if (o->dry_run) {
        fprintf(out, "\ndry-run: no keys deleted\n");
        goto cleanup;
    }

    /* Regular deletions behind ONE bulk prompt (--yes skips it). */
    if (nreg > 0) {
        if (!o->assume_yes && !confirm_bulk(in, out, nreg)) {
            fprintf(out, "aborted regular deletion\n");
            if (nexact == 0) {
                exit_code = HEM_KEYS_RUNTIME;   /* user abort, nothing else to do */
                goto cleanup;
            }
            nreg = 0;                           /* fall through to protected keys */
        }
        for (i = 0; i < nreg; i++) {
            const ehem_key_entry *e = regular[i];
            rc = ehem_key_delete(ctx, e->kid);
            if (rc == EHEM_OK) {
                fprintf(out, "deleted %s  '%s'\n",
                        e->kid, (e->label != NULL) ? e->label : "");
            } else {
                reg_fail++;
                fprintf(err, "FAILED  %s  '%s': %s\n",
                        e->kid, (e->label != NULL) ? e->label : "",
                        ehem_rc_str(rc));
            }
        }
    }

    /* Protected deletions: ALWAYS interactive; --yes never applies. */
    for (i = 0; i < nexact; i++) {
        const ehem_key_entry *e = prot_exact[i];
        if (!confirm_protected(in, out, e)) {
            prot_skip++;
            fprintf(out, "skipped %s  '%s'\n",
                    e->kid, (e->label != NULL) ? e->label : "");
            continue;
        }
        rc = ehem_key_delete(ctx, e->kid);
        if (rc == EHEM_OK) {
            fprintf(out, "deleted PROTECTED %s  '%s'\n",
                    e->kid, (e->label != NULL) ? e->label : "");
        } else {
            prot_fail++;
            fprintf(err, "FAILED  PROTECTED %s  '%s': %s\n",
                    e->kid, (e->label != NULL) ? e->label : "", ehem_rc_str(rc));
        }
    }

    fprintf(out, "\ndone: %lu deleted, %lu failed, %lu protected skipped\n",
            (unsigned long)(nreg - reg_fail + nexact - prot_fail - prot_skip),
            (unsigned long)(reg_fail + prot_fail),
            (unsigned long)prot_skip);
    if (reg_fail > 0 || prot_fail > 0) {
        exit_code = HEM_KEYS_RUNTIME;
    }

cleanup:
    free(regular);
    free(prot_exact);
    free(prot_partial);
    ehem_key_page_free(page);
    return exit_code;
}
