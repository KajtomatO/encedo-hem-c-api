/*
 * tool_auth.c — shared login chokepoint + mobile-outcome exit mapping.
 *
 * implements: REQ-TOOL-018
 */
#include "tool_auth.h"

static FILE *or_stderr(FILE *f) { return (f != NULL) ? f : stderr; }

ehem_rc hem_tool_login(ehem_ctx *ctx, const char *passphrase, bool mobile,
                       FILE *err_in)
{
    FILE *err = or_stderr(err_in);
    ehem_rc rc;

    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    if (mobile) {
        rc = ehem_login_mobile(ctx);
    } else if (passphrase != NULL) {
        rc = ehem_login(ctx, passphrase);
    } else {
        fprintf(err, "error: needs a passphrase (--passphrase / "
                     "EHEM_PASSPHRASE) or --mobile\n");
        return EHEM_ERR_ARG;
    }
    if (rc != EHEM_OK) {
        const ehem_error *e = ehem_last_error(ctx);
        fprintf(err, "error: login: %s\n",
                (e != NULL && e->message[0] != '\0') ? e->message
                                                     : ehem_rc_str(rc));
    }
    return rc;
}

int hem_tool_auth_exit(ehem_rc rc, FILE *err_in, int fallback)
{
    FILE *err = or_stderr(err_in);

    if (rc == EHEM_ERR_CONFIRM_TIMEOUT) {
        fprintf(err, "no answer on the phone in time — is an authenticator "
                     "paired? (hem-tool ext list)\n");
        return HEM_TOOL_EXIT_CONFIRM_TIMEOUT;
    }
    if (rc == EHEM_ERR_USER_REJECTED) {
        fprintf(err, "rejected on the phone\n");
        return HEM_TOOL_EXIT_REJECTED;
    }
    return fallback;
}
