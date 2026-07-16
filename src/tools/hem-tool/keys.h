/*
 * keys.h — hem-tool `keys` subcommands + the protected-key classifier.
 *
 * implements: REQ-TOOL-004 (`keys list` — read-only inventory with marking),
 *             REQ-TOOL-005 (label-based protected-key policy),
 *             REQ-TOOL-006 (`keys rm` — removal with the protected-key guard),
 *             REQ-TOOL-007 (`keys pub` — public material + typed metadata),
 *             REQ-TOOL-009 (`keys gen` — generate a key on the device)
 *
 * Lives in hem-tool-core (like cert_install.c) so the CLI and the unit tests
 * drive one code path, using ONLY the public SDK API. `keys list` (marking) and
 * `keys rm` (guarding) share hem_key_is_protected() — the single source of the
 * protected-label policy (no duplicated label constants anywhere else).
 */
#ifndef HEM_KEYS_H
#define HEM_KEYS_H

#include <stdbool.h>
#include <stdint.h>
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

/* Small helpers shared across hem-tool-core subcommands (keys pub, sign):
 * kid validation (32 hex chars) and material/output encoders. */
bool hem_tool_kid_ok(const char *s);
void hem_tool_fprint_b64(FILE *f, const uint8_t *raw, size_t len);
void hem_tool_fprint_hex(FILE *f, const uint8_t *raw, size_t len);

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

/* Output format for `keys pub` (REQ-TOOL-007). */
typedef enum {
    HEM_KEYS_PUB_B64 = 0,   /* human summary, material as padded base64 */
    HEM_KEYS_PUB_HEX,       /* human summary, material as lowercase hex */
    HEM_KEYS_PUB_RAW        /* material bytes ONLY on out (pipeline use) */
} hem_keys_pub_format;

typedef struct {
    const char         *passphrase;  /* login passphrase; NULL → HEM_KEYS_USAGE */
    const char         *kid;         /* 32 hex chars; NULL/malformed → USAGE, no I/O */
    hem_keys_pub_format format;
    FILE               *out;         /* summary / raw material (NULL → stdout) */
    FILE               *err;         /* diagnostics (NULL → stderr) */
} hem_keys_pub_opts;

/*
 * `hem-tool keys pub <kid>` (REQ-TOOL-007): log in, fetch the key via
 * ehem_key_get (read-only), and print a human summary — the device type
 * string, the REQ-KEY-006 classification (family, modes), the update
 * timestamp, and the public material (`pubkey` or `der`) as padded base64
 * (HEX: lowercase hex instead). RAW writes the raw material bytes ALONE to
 * o->out (prose only on o->err) so pipes stay clean. A symmetric key (no
 * material on the wire) prints its metadata plus an explicit note and still
 * returns HEM_KEYS_OK — the get succeeded. Key not found (device 406) is
 * HEM_KEYS_RUNTIME with a message naming the kid.
 */
int hem_keys_pub_run(ehem_ctx *ctx, const hem_keys_pub_opts *o);

/* Options for `keys gen` (REQ-TOOL-009). `type` and `label` are required. */
typedef struct {
    const char *passphrase;   /* login passphrase; NULL → HEM_KEYS_USAGE */
    const char *type;         /* device type literal; NULL → USAGE */
    const char *label;        /* key label (SDK validates); NULL → USAGE */
    const char *descr;        /* optional descr — raw bytes are these string
                                 bytes; NULL omits it */
    const char *mode;         /* "ECDH"|"ExDSA"|"ECDH,ExDSA"; NULL → auto (the
                                 tool sends ECDH,ExDSA for NIST-P/K, else none) */
    FILE       *out;          /* the new kid (NULL → stdout) */
    FILE       *err;          /* diagnostics + the default-mode note (NULL → stderr) */
} hem_keys_gen_opts;

/*
 * `hem-tool keys gen <TYPE>` (REQ-TOOL-009): log in and create a key via
 * ehem_key_create, printing the new 32-hex kid to o->out. `type` is passed
 * verbatim (no tool allowlist). When o->mode is NULL and TYPE classifies to a
 * NIST-P/K family (REQ-KEY-006), the tool sends mode "ECDH,ExDSA" and notes it
 * on o->err — the device default is ECDH-only, which cannot sign (python
 * OQ-19); for every other family an omitted mode sends no mode field. A mode
 * that is not one of the three exact literals is HEM_KEYS_USAGE with no I/O.
 * The SDK's label/descr validation surfaces as HEM_KEYS_USAGE (EHEM_ERR_ARG);
 * a device 400/406 is HEM_KEYS_RUNTIME with the status in the message.
 */
int hem_keys_gen_run(ehem_ctx *ctx, const hem_keys_gen_opts *o);

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
