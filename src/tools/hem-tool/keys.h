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

#endif /* HEM_KEYS_H */
