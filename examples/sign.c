/*
 * sign.c — an authenticated SDK program: log in with the passphrase, find a
 * key by its label, sign a message with it, print the signature as hex.
 *
 * implements: REQ-API-007 (the runnable examples; until the SDK's 1.2.1 that
 *             role was hem-tool's, now encedo-hem-tool's)
 *
 *   sign LABEL [MESSAGE]     EHEM_PASSPHRASE required; EHEM_URL optional
 *                            (default https://my.ence.do); MESSAGE defaults
 *                            to a fixed sentence
 *
 * The authenticated shape: ehem_login() only records the credential; the
 * first authenticated call — here ehem_key_list_all(), scope keymgmt:list —
 * runs the challenge/derive/token exchange and caches the bearer per scope,
 * so a wrong passphrase surfaces THERE, as EHEM_ERR_AUTH_FAILED. ehem_sign()
 * then needs the per-key scope keymgmt:use:<kid>, acquired the same way. The
 * selector (`alg`) follows the key's type through the SDK's own classifier
 * (ehem_key_type_parse); the device, not the SDK, decides whether that key
 * may sign (a NIST key without the ExDSA mode is its 406). Exit 0 with the
 * signature printed, 1 on any failure (the SDK's detail on stderr), 2 on
 * usage. Public API only, as any consumer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/keymgmt.h"
#include "ehem/crypto.h"

/* The device's `alg` selector for a key type, or NULL when the type cannot
 * sign at all (ECDH-only curves, symmetric keys, certificates, ...). */
static const char *selector_for(const char *type)
{
    ehem_key_type_info info;

    if (ehem_key_type_parse(type, &info) != EHEM_OK) {
        return NULL;
    }
    switch (info.family) {
    case EHEM_KEY_FAMILY_ED25519:   return EHEM_SIGN_ALG_ED25519;
    case EHEM_KEY_FAMILY_ED448:     return EHEM_SIGN_ALG_ED448;
    case EHEM_KEY_FAMILY_SECP256R1:
    case EHEM_KEY_FAMILY_SECP256K1: return EHEM_SIGN_ALG_SHA256_ECDSA;
    case EHEM_KEY_FAMILY_SECP384R1: return EHEM_SIGN_ALG_SHA384_ECDSA;
    case EHEM_KEY_FAMILY_SECP521R1: return EHEM_SIGN_ALG_SHA512_ECDSA;
    default:                        return NULL;
    }
}

static int report(ehem_ctx *ctx, const char *what, ehem_rc rc)
{
    const ehem_error *err = ehem_last_error(ctx);

    fprintf(stderr, "sign: %s: %s", what, ehem_rc_str(rc));
    if (err != NULL && err->message[0] != '\0') {
        fprintf(stderr, " - %s", err->message);
    }
    if (err != NULL && err->http_status != 0 && strstr(err->message, "HTTP") == NULL) {
        fprintf(stderr, " (HTTP %ld)", err->http_status);
    }
    fputc('\n', stderr);
    return 1;
}

int main(int argc, char **argv)
{
    const char *passphrase = getenv("EHEM_PASSPHRASE");
    const char *url = getenv("EHEM_URL");
    const char *label;
    const char *msg;
    const char *alg;
    size_t msg_len, i;
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_key_page *page = NULL;
    const ehem_key_entry *key = NULL;
    ehem_signature *sig = NULL;
    ehem_rc rc;
    int code = 1;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: sign LABEL [MESSAGE]   (EHEM_PASSPHRASE, EHEM_URL)\n");
        return 2;
    }
    if (passphrase == NULL || passphrase[0] == '\0') {
        fprintf(stderr, "sign: EHEM_PASSPHRASE is not set\n");
        return 2;
    }
    label   = argv[1];
    msg     = argc > 2 ? argv[2] : "hello from the Encedo HEM C SDK";
    msg_len = strlen(msg);
    if (msg_len == 0 || msg_len > EHEM_SIGN_MSG_MAX) {
        fprintf(stderr, "sign: the message must be 1..%d bytes\n", EHEM_SIGN_MSG_MAX);
        return 2;
    }
    if (url == NULL || url[0] == '\0') {
        url = "https://my.ence.do";
    }

    rc = ehem_global_init();
    if (rc != EHEM_OK) {
        fprintf(stderr, "sign: ehem_global_init: %s\n", ehem_rc_str(rc));
        return 1;
    }
    ehem_options_init(&opts);
    rc = ehem_ctx_create(url, &opts, &ctx);
    if (rc != EHEM_OK) {
        fprintf(stderr, "sign: ehem_ctx_create(%s): %s\n", url, ehem_rc_str(rc));
        ehem_global_cleanup();
        return 1;
    }
    rc = ehem_login(ctx, passphrase);        /* records the credential only */
    if (rc != EHEM_OK) {
        code = report(ctx, "ehem_login", rc);
        goto out;
    }

    /* The whole repository, merged into one page; the first authenticated
     * call, so a wrong passphrase fails here. */
    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        code = report(ctx, "ehem_key_list_all", rc);
        goto out;
    }
    for (i = 0; i < page->listed; i++) {
        if (page->entries[i].label != NULL &&
            strcmp(page->entries[i].label, label) == 0) {
            key = &page->entries[i];
            break;
        }
    }
    if (key == NULL) {
        fprintf(stderr, "sign: no key labelled '%s' among the %ld on the device\n",
                label, (long)page->total);
        goto out;
    }
    alg = selector_for(key->type);
    if (alg == NULL) {
        fprintf(stderr, "sign: key '%s' is a %s - not a signing type this example "
                        "knows\n", label, key->type);
        goto out;
    }

    rc = ehem_sign(ctx, key->kid, alg, (const uint8_t *)msg, msg_len,
                   NULL, 0, &sig);
    if (rc != EHEM_OK) {
        code = report(ctx, "ehem_sign", rc);
        goto out;
    }

    printf("key:  %s (%s, kid %s)\n", label, key->type, key->kid);
    printf("alg:  %s\n", alg);
    printf("msg:  %lu bytes\n", (unsigned long)msg_len);
    printf("sig:  ");
    for (i = 0; i < sig->sig_len; i++) {
        printf("%02x", sig->sig[i]);
    }
    printf("  (%lu bytes)\n", (unsigned long)sig->sig_len);
    code = 0;

out:
    ehem_signature_free(sig);
    ehem_key_page_free(page);
    ehem_ctx_destroy(ctx);                   /* implicit logout: credential zeroized */
    ehem_global_cleanup();
    return code;
}
