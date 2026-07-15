/*
 * fake_transport.c — implementation of the offline unit-test transport.
 *
 * supports: REQ-TEST-001, REQ-NET-001
 */
#include "fake_transport.h"

#include <stdlib.h>
#include <string.h>

/* A queued canned outcome. */
typedef struct scripted_response {
    ehem_rc rc;        /* EHEM_OK → deliver status/body; else a transport error */
    long    status;
    char   *body;      /* owned copy, or NULL */
    size_t  body_len;
} scripted_response;

typedef struct fake_state {
    /* Response script (FIFO consumed by an advancing cursor). */
    scripted_response *responses;
    size_t             resp_count;
    size_t             resp_cursor;
    ehem_rc            default_rc;   /* used when the script is exhausted */

    /* Captured outgoing requests. */
    fake_captured_request *requests;
    size_t                 req_count;

    /* Detail string returned by ehem_transport_last_detail after a failure. */
    char                  *detail;
} fake_state;

/* --- small owned-copy helpers -------------------------------------------- */

static char *dup_str(const char *s)
{
    size_t n;
    char *p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* Copy `len` bytes and NUL-terminate; NULL/0 → NULL. */
static uint8_t *dup_bytes(const uint8_t *b, size_t len)
{
    uint8_t *p;
    if (b == NULL || len == 0) {
        return NULL;
    }
    p = malloc(len + 1);
    if (p != NULL) {
        memcpy(p, b, len);
        p[len] = '\0';
    }
    return p;
}

static void free_captured(fake_captured_request *r)
{
    size_t i;
    free(r->path);
    for (i = 0; i < r->header_count; i++) {
        free((void *)r->headers[i].name);
        free((void *)r->headers[i].value);
    }
    free(r->headers);
    free(r->body);
}

/* --- capture the outgoing request ---------------------------------------- */

/* Append a deep copy of `req` to the capture list. Best-effort: on allocation
 * failure the request is simply not recorded (send still proceeds). */
static void capture_request(fake_state *st, const ehem_request *req)
{
    fake_captured_request *grown;
    fake_captured_request *rec;
    size_t i;

    grown = realloc(st->requests, (st->req_count + 1) * sizeof *grown);
    if (grown == NULL) {
        return;
    }
    st->requests = grown;
    rec = &st->requests[st->req_count];
    memset(rec, 0, sizeof *rec);

    rec->method   = req->method;
    rec->path     = dup_str(req->path);
    rec->body     = dup_bytes(req->body, req->body_len);
    rec->body_len = (rec->body != NULL) ? req->body_len : 0;

    if (req->header_count > 0 && req->headers != NULL) {
        rec->headers = calloc(req->header_count, sizeof *rec->headers);
        if (rec->headers != NULL) {
            for (i = 0; i < req->header_count; i++) {
                rec->headers[i].name  = dup_str(req->headers[i].name);
                rec->headers[i].value = dup_str(req->headers[i].value);
            }
            rec->header_count = req->header_count;
        }
    }
    st->req_count++;
}

/* --- vtable ops ----------------------------------------------------------- */

static ehem_rc fake_send(void *state, const ehem_request *req, ehem_response *resp)
{
    fake_state *st = (fake_state *)state;
    scripted_response *scr;

    memset(resp, 0, sizeof *resp);
    capture_request(st, req);

    if (st->resp_cursor >= st->resp_count) {
        return st->default_rc;   /* script exhausted */
    }
    scr = &st->responses[st->resp_cursor++];

    if (scr->rc != EHEM_OK) {
        return scr->rc;          /* simulated transport-level failure */
    }

    resp->status = scr->status;
    if (scr->body != NULL) {
        resp->body = dup_bytes((const uint8_t *)scr->body, scr->body_len);
        if (resp->body == NULL) {
            return EHEM_ERR_NOMEM;
        }
        resp->body_len = scr->body_len;
    }
    return EHEM_OK;
}

static const char *fake_last_detail(void *state)
{
    const fake_state *st = (const fake_state *)state;
    return (st != NULL && st->detail != NULL) ? st->detail : "";
}

static void fake_destroy(void *state)
{
    fake_state *st = (fake_state *)state;
    size_t i;
    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->resp_count; i++) {
        free(st->responses[i].body);
    }
    free(st->responses);
    for (i = 0; i < st->req_count; i++) {
        free_captured(&st->requests[i]);
    }
    free(st->requests);
    free(st->detail);
    free(st);
}

static const ehem_transport_ops FAKE_OPS = {
    fake_send,
    fake_last_detail,
    fake_destroy,
};

/* --- public constructor / inspectors ------------------------------------- */

ehem_transport *fake_transport_new(void)
{
    ehem_transport *t;
    fake_state *st;

    t = calloc(1, sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    st = calloc(1, sizeof *st);
    if (st == NULL) {
        free(t);
        return NULL;
    }
    st->default_rc = EHEM_ERR_NETWORK;
    t->ops   = &FAKE_OPS;
    t->state = st;
    return t;
}

void fake_transport_free(ehem_transport *t)
{
    ehem_transport_destroy(t);   /* runs FAKE_OPS.destroy then frees the wrapper */
}

int fake_transport_push_response(ehem_transport *t, ehem_rc rc,
                                 long status, const char *body)
{
    fake_state *st;
    scripted_response *grown;
    scripted_response *slot;

    if (t == NULL || t->state == NULL) {
        return -1;
    }
    st = (fake_state *)t->state;

    grown = realloc(st->responses, (st->resp_count + 1) * sizeof *grown);
    if (grown == NULL) {
        return -1;
    }
    st->responses = grown;
    slot = &st->responses[st->resp_count];
    memset(slot, 0, sizeof *slot);
    slot->rc     = rc;
    slot->status = status;
    if (body != NULL) {
        slot->body = dup_str(body);
        if (slot->body == NULL) {
            return -1;
        }
        slot->body_len = strlen(body);
    }
    st->resp_count++;
    return 0;
}

void fake_transport_set_default_rc(ehem_transport *t, ehem_rc rc)
{
    if (t != NULL && t->state != NULL) {
        ((fake_state *)t->state)->default_rc = rc;
    }
}

void fake_transport_set_detail(ehem_transport *t, const char *detail)
{
    fake_state *st;
    if (t == NULL || t->state == NULL) {
        return;
    }
    st = (fake_state *)t->state;
    free(st->detail);
    st->detail = dup_str(detail);
}

size_t fake_transport_request_count(const ehem_transport *t)
{
    if (t == NULL || t->state == NULL) {
        return 0;
    }
    return ((const fake_state *)t->state)->req_count;
}

const fake_captured_request *fake_transport_request(const ehem_transport *t,
                                                    size_t i)
{
    const fake_state *st;
    if (t == NULL || t->state == NULL) {
        return NULL;
    }
    st = (const fake_state *)t->state;
    if (i >= st->req_count) {
        return NULL;
    }
    return &st->requests[i];
}

const char *fake_transport_request_header(const ehem_transport *t, size_t i,
                                          const char *name)
{
    const fake_captured_request *r = fake_transport_request(t, i);
    size_t h;
    if (r == NULL || name == NULL) {
        return NULL;
    }
    for (h = 0; h < r->header_count; h++) {
        if (r->headers[h].name != NULL && strcmp(r->headers[h].name, name) == 0) {
            return r->headers[h].value;
        }
    }
    return NULL;
}
