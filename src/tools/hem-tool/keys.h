/*
 * keys.h — hem-tool `keys` subcommands + the protected-key classifier.
 *
 * implements: REQ-TOOL-005 (label-based protected-key policy),
 *             REQ-TOOL-004 (`keys list` — read-only inventory with marking)
 *
 * Lives in hem-tool-core (like cert_install.c) so the CLI and the unit tests
 * drive one code path, using ONLY the public SDK API. `keys rm` (REQ-TOOL-006)
 * will land here too and reuse hem_key_is_protected() — the single source of
 * the protected-label policy (no duplicated label constants anywhere else).
 */
#ifndef HEM_KEYS_H
#define HEM_KEYS_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes shared by the keys subcommands (REQ-TOOL-004). */
enum {
    HEM_KEYS_OK      = 0,  /* success */
    HEM_KEYS_RUNTIME = 1,  /* login / list failure (message printed) */
    HEM_KEYS_USAGE   = 2   /* missing passphrase / environment */
};

typedef struct {
    const char *passphrase;   /* login passphrase; NULL → HEM_KEYS_USAGE */
    FILE       *out;          /* listing output (NULL → stdout) */
    FILE       *err;          /* diagnostics (NULL → stderr) */
} hem_keys_opts;

/*
 * Protected-key classifier (REQ-TOOL-005) — by LABEL only (a client-side
 * convention; the same algorithm legitimately appears on non-protected keys).
 * True iff `label` is exactly "TLS PrivateKey" or "TLS Certificate" (the
 * device's own TLS material), or contains "(Android)" / "(iPhone)"
 * case-insensitively (paired phone authenticators). NULL → false.
 *
 * The ONE place the protected-label policy is spelled out — keys list (marking),
 * keys rm (guarding), and the tests all call this.
 */
bool hem_key_is_protected(const char *label);

/*
 * `hem-tool keys list` (REQ-TOOL-004): log in, walk the whole repository
 * (read-only), and print each key as "<kid>  '<label>'  (<type>)" with
 * protected keys marked "[PROTECTED]", then a summary line (total + protected
 * counts). Returns a HEM_KEYS_* code; output goes to o->out / o->err.
 */
int hem_keys_list_run(ehem_ctx *ctx, const hem_keys_opts *o);

/* Options for `keys rm` (REQ-TOOL-006). Selection is `all` XOR one-or-more
 * `prefixes` (neither / both → HEM_KEYS_USAGE). */
typedef struct {
    const char        *passphrase;   /* login passphrase; NULL → HEM_KEYS_USAGE */
    int                all;          /* --all: every non-protected key */
    const char *const *prefixes;     /* --label-prefix values (label prefixes) */
    size_t             prefix_count;
    int                dry_run;      /* --dry-run: report, delete nothing */
    int                assume_yes;   /* --yes: skip the bulk prompt (never protected) */
    FILE              *out;          /* report + progress (NULL → stdout) */
    FILE              *err;          /* diagnostics (NULL → stderr) */
    FILE              *in;           /* confirmation input (NULL → stdin) */
} hem_keys_rm_opts;

/*
 * `hem-tool keys rm` (REQ-TOOL-006), with wipe_keys.py semantics: partition the
 * repository into regular targets, protected targets (a protected key is a
 * target only when some prefix equals its label EXACTLY), and protected keys
 * skipped as partial matches; print the partition; then (unless --dry-run)
 * delete regular targets behind ONE bulk prompt (--yes skips it) and each
 * protected target behind its own literal-"YES" prompt (--yes never applies).
 * A failed delete is reported and processing continues. Returns HEM_KEYS_OK (0)
 * on success / dry-run / nothing-to-do, HEM_KEYS_RUNTIME (1) on a user abort or
 * any failed delete, HEM_KEYS_USAGE (2) on a selection/passphrase error.
 */
int hem_keys_rm_run(ehem_ctx *ctx, const hem_keys_rm_opts *o);

#endif /* HEM_KEYS_H */
