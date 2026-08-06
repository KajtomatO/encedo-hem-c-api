/*
 * transport.c — transport vtable dispatch and response ownership helpers.
 *
 * implements: REQ-NET-001
 *
 * Implementation-agnostic glue only: no libcurl here (that is transport_curl.c,
 * STEP-M1-060) and no fake here (tests/support). This file just dispatches
 * through the ops table and owns the response-lifetime rules.
 */
#include "transport.h"

#include <stdlib.h>

ehem_rc ehem_transport_send(const ehem_transport *t,
                            const ehem_request *req,
                            ehem_response *resp)
{
    if (t == NULL || t->ops == NULL || t->ops->send == NULL ||
        req == NULL || resp == NULL) {
        return EHEM_ERR_ARG;
    }
    return t->ops->send(t->state, req, resp);
}

const char *ehem_transport_last_detail(const ehem_transport *t)
{
    if (t != NULL && t->ops != NULL && t->ops->last_detail != NULL) {
        const char *d = t->ops->last_detail(t->state);
        if (d != NULL) {
            return d;
        }
    }
    return "";
}

int ehem_transport_last_tls_expired(const ehem_transport *t)
{
    if (t != NULL && t->ops != NULL && t->ops->last_tls_expired != NULL) {
        return t->ops->last_tls_expired(t->state);
    }
    return 0;
}

void ehem_transport_destroy(ehem_transport *t)
{
    if (t == NULL) {
        return;
    }
    if (t->ops != NULL && t->ops->destroy != NULL) {
        t->ops->destroy(t->state);
    }
    free(t);
}

void ehem_response_free(ehem_response *resp)
{
    size_t i;

    if (resp == NULL) {
        return;
    }
    for (i = 0; i < resp->header_count; i++) {
        /* Response header strings are owned (see ehem_header doc). */
        free((void *)resp->headers[i].name);
        free((void *)resp->headers[i].value);
    }
    free(resp->headers);
    free(resp->body);

    resp->status       = 0;
    resp->headers      = NULL;
    resp->header_count = 0;
    resp->body         = NULL;
    resp->body_len     = 0;
}
