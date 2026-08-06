/*
 * proto_storage.c — bindings for the `storage` API group: USB-MSC partition
 * unlock/lock. The firmware reads the disk index and access mode from the
 * JWT SCOPE, not the URL, so these bindings compose the scope string from
 * their arguments and use the plain paths (the HEM-test-suite-proven form;
 * the /ro and /rw URL suffixes exist only as a Manager-side narrowing
 * override).
 *
 * implements: REQ-SYS-010
 *
 * Firmware caution (REQ-SYS-010): both v1.2.2 handlers read an
 * UNINITIALIZED `sub` pointer in their scope check (api_storage.c:44-46 /
 * :132-134) — behavior beyond the scope-prefix match is formally undefined
 * device-side; the live probe runs attended first.
 */
#include "ehem/storage.h"

#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "proto_common.h"
#include "transport.h"

/* GET with empty-200-as-success (the storage endpoints send no body). */
static ehem_rc storage_call(ehem_ctx *ctx, const char *path, const char *scope,
                            const char *what)
{
    char *body = NULL;
    ehem_rc rc;

    (void)what;
    rc = ehem_proto_request_raw(ctx, EHEM_HTTP_GET, path, NULL, scope,
                                EHEM_TLS_REQ_DEFAULT, &body);
    if (rc == EHEM_OK) {
        free(body);                 /* no body expected; tolerate one */
        ehem_ctx_clear_error(ctx);
        return EHEM_OK;
    }
    if (rc == EHEM_ERR_PROTOCOL && ehem_last_error(ctx)->http_status == 200) {
        ehem_ctx_clear_error(ctx);  /* empty 200 IS the success shape */
        return EHEM_OK;
    }
    return rc;   /* 403/406/409/404(EPA)/transport — mapped with detail */
}

ehem_rc ehem_storage_unlock(ehem_ctx *ctx, int disk, int writable)
{
    char scope[32];

    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);
    if (disk < 0 || disk > 2) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "storage/unlock: disk must be 0..2");
    }
    /* The scope carries the arguments: storage:disk<N>[:rw]. */
    snprintf(scope, sizeof scope, "storage:disk%d%s", disk,
             writable ? ":rw" : "");
    return storage_call(ctx, "/api/storage/unlock", scope, "unlock");
}

ehem_rc ehem_storage_lock(ehem_ctx *ctx, int disk)
{
    char scope[32];

    if (ctx == NULL) {
        return EHEM_ERR_ARG;
    }
    ehem_ctx_clear_error(ctx);
    if (disk < 0 || disk > 2) {
        return ehem_ctx_fail(ctx, EHEM_ERR_ARG, 0, NULL,
                             "storage/lock: disk must be 0..2");
    }
    snprintf(scope, sizeof scope, "storage:disk%d", disk);
    return storage_call(ctx, "/api/storage/lock", scope, "lock");
}
