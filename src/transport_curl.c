/*
 * transport_curl.c — the built-in default transport, implemented with libcurl.
 *
 * implements: REQ-NET-002, REQ-NET-003, REQ-NET-004
 *
 * One curl easy handle per context (connection reuse across requests). All
 * libcurl usage in the library is confined to this file; the vtable it fills
 * (transport.h) is what the rest of the code sees.
 */
#include "transport.h"
#include "transport_curl.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EHEM_VERSION_STRING
/* Normally injected by the build from PROJECT_VERSION (see CMakeLists). */
#define EHEM_VERSION_STRING "0.0.0-dev"
#endif

/* Per-context transport state. */
typedef struct curl_state {
    CURL  *handle;                 /* reused across sends → connection reuse */
    char  *base_url;               /* owned; requests append their path to this */
    char   errbuf[CURL_ERROR_SIZE];/* libcurl's per-transfer error detail */
    char   detail[CURL_ERROR_SIZE];/* last_detail() view (errbuf or strerror) */
} curl_state;

/* Growable byte buffer for the response body write callback. */
typedef struct body_buf {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    bool     oom;
} body_buf;

/* -------------------------------------------------------------------------- */
/* Error translation (REQ-API-003, REQ-NET-004) — pure, unit-tested directly  */
/* -------------------------------------------------------------------------- */

ehem_rc ehem_curl_map_error(CURLcode code, double connect_time)
{
    switch (code) {
    case CURLE_OK:
        return EHEM_OK;

    /* Never reached the peer: unreachable-class. */
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_CONNECT:
#ifdef CURLE_WEIRD_SERVER_REPLY
    case CURLE_WEIRD_SERVER_REPLY:  /* connected socket but no usable reply at all */
#endif
        return EHEM_ERR_UNREACHABLE;

    case CURLE_OPERATION_TIMEDOUT:
        /* libcurl uses one code for connect and total timeouts; a connection
         * that never established (connect_time == 0) is a connect timeout. */
        return (connect_time <= 0.0) ? EHEM_ERR_UNREACHABLE : EHEM_ERR_NETWORK;

    /* Caller/programming errors surfaced by libcurl. */
    case CURLE_OUT_OF_MEMORY:
        return EHEM_ERR_NOMEM;
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL:
        return EHEM_ERR_ARG;

    /* Connected, then the exchange (incl. TLS handshake/verification) failed:
     * network-class. REQ-NET-003 allows a verification failure to be either
     * class; we report NETWORK since the TCP connection was established. */
    default:
        return EHEM_ERR_NETWORK;
    }
}

/* -------------------------------------------------------------------------- */
/* libcurl callbacks                                                          */
/* -------------------------------------------------------------------------- */

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    body_buf *b = (body_buf *)userdata;
    size_t add = size * nmemb;

    if (b->len + add + 1 > b->cap) {
        size_t newcap = (b->cap != 0) ? b->cap * 2 : 1024;
        uint8_t *grown;
        while (newcap < b->len + add + 1) {
            newcap *= 2;
        }
        grown = realloc(b->data, newcap);
        if (grown == NULL) {
            b->oom = true;
            return 0;   /* signals write error → curl aborts the transfer */
        }
        b->data = grown;
        b->cap  = newcap;
    }
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';   /* keep the body NUL-terminated for convenience */
    return nmemb;
}

/* -------------------------------------------------------------------------- */
/* URL joining                                                                */
/* -------------------------------------------------------------------------- */

/* Join base + path with exactly one '/' between them. Caller frees. */
static char *join_url(const char *base, const char *path)
{
    size_t bl = strlen(base);
    size_t pl = (path != NULL) ? strlen(path) : 0;
    bool base_slash = (bl > 0 && base[bl - 1] == '/');
    bool path_slash = (pl > 0 && path[0] == '/');
    bool need_sep = (pl > 0 && !base_slash && !path_slash);
    bool drop_one = (base_slash && path_slash);
    size_t total;
    char *out;

    if (path == NULL) {
        path = "";
    }
    total = bl + pl + (need_sep ? 1 : 0) - (drop_one ? 1 : 0) + 1;
    out = malloc(total);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, base, bl);
    if (need_sep) {
        out[bl] = '/';
        memcpy(out + bl + 1, path, pl);
        out[bl + 1 + pl] = '\0';
    } else if (drop_one) {
        memcpy(out + bl, path + 1, pl - 1);
        out[bl + pl - 1] = '\0';
    } else {
        memcpy(out + bl, path, pl);
        out[bl + pl] = '\0';
    }
    return out;
}

/* -------------------------------------------------------------------------- */
/* vtable ops                                                                 */
/* -------------------------------------------------------------------------- */

/* Build a curl_slist from the request headers. On success *out is the list (may
 * be NULL for zero headers); returns false only on allocation failure. */
static bool build_headers(const ehem_request *req, struct curl_slist **out)
{
    struct curl_slist *list = NULL;
    size_t i;

    for (i = 0; i < req->header_count; i++) {
        const ehem_header *h = &req->headers[i];
        size_t n;
        char *line;
        struct curl_slist *tmp;
        if (h->name == NULL) {
            continue;
        }
        /* "Name: value" */
        n = strlen(h->name) + 2 + (h->value ? strlen(h->value) : 0) + 1;
        line = malloc(n);
        if (line == NULL) {
            curl_slist_free_all(list);
            return false;
        }
        if (h->value != NULL) {
            snprintf(line, n, "%s: %s", h->name, h->value);
        } else {
            snprintf(line, n, "%s:", h->name);   /* header with empty value */
        }
        tmp = curl_slist_append(list, line);
        free(line);
        if (tmp == NULL) {
            curl_slist_free_all(list);
            return false;
        }
        list = tmp;
    }
    *out = list;
    return true;
}

/* Reset the easy handle's method-sticky options to a clean GET baseline so a
 * reused handle never carries a previous request's method/body. */
static void reset_method(CURL *h)
{
    curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, (char *)NULL);
    curl_easy_setopt(h, CURLOPT_POST, 0L);
    curl_easy_setopt(h, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
}

static void apply_method(CURL *h, const ehem_request *req)
{
    const char *body = (req->body != NULL) ? (const char *)req->body : "";
    long body_len = (long)req->body_len;

    switch (req->method) {
    case EHEM_HTTP_GET:
        /* baseline already GET */
        break;
    case EHEM_HTTP_POST:
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, body_len);
        break;
    case EHEM_HTTP_PUT:
    case EHEM_HTTP_DELETE:
    case EHEM_HTTP_PATCH:
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST,
                         req->method == EHEM_HTTP_PUT    ? "PUT" :
                         req->method == EHEM_HTTP_DELETE ? "DELETE" : "PATCH");
        if (req->body != NULL) {
            /* POSTFIELDS works with CUSTOMREQUEST for a body-carrying verb. */
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, body_len);
        }
        break;
    }
}

static ehem_rc curl_send(void *state, const ehem_request *req, ehem_response *resp)
{
    curl_state *st = (curl_state *)state;
    CURL *h = st->handle;
    struct curl_slist *hdrs = NULL;
    body_buf body = { NULL, 0, 0, false };
    char *url;
    CURLcode cc;
    ehem_rc rc;

    memset(resp, 0, sizeof *resp);
    st->errbuf[0] = '\0';
    st->detail[0] = '\0';

    url = join_url(st->base_url, req->path);
    if (url == NULL) {
        return EHEM_ERR_NOMEM;
    }
    if (!build_headers(req, &hdrs)) {
        free(url);
        return EHEM_ERR_NOMEM;
    }

    reset_method(h);
    apply_method(h, req);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    if (req->connect_timeout_ms > 0) {
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, (long)req->connect_timeout_ms);
    }
    if (req->total_timeout_ms > 0) {
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, (long)req->total_timeout_ms);
    }
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);

    cc = curl_easy_perform(h);

    curl_slist_free_all(hdrs);
    free(url);

    if (cc != CURLE_OK) {
        double connect_time = 0.0;
        const char *msg;
        curl_easy_getinfo(h, CURLINFO_CONNECT_TIME, &connect_time);
        /* A write-callback OOM abort surfaces as CURLE_WRITE_ERROR. */
        if (body.oom) {
            free(body.data);
            return EHEM_ERR_NOMEM;
        }
        free(body.data);
        msg = (st->errbuf[0] != '\0') ? st->errbuf : curl_easy_strerror(cc);
        snprintf(st->detail, sizeof st->detail, "%s", msg);
        return ehem_curl_map_error(cc, connect_time);
    }

    rc = EHEM_OK;
    {
        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        resp->status   = status;
        resp->body     = body.data;   /* may be NULL for an empty body */
        resp->body_len = body.len;
    }
    return rc;
}

static const char *curl_last_detail(void *state)
{
    curl_state *st = (curl_state *)state;
    return st->detail;
}

static void curl_destroy(void *state)
{
    curl_state *st = (curl_state *)state;
    if (st == NULL) {
        return;
    }
    if (st->handle != NULL) {
        curl_easy_cleanup(st->handle);
    }
    free(st->base_url);
    free(st);
}

static const ehem_transport_ops CURL_OPS = {
    curl_send,
    curl_last_detail,
    curl_destroy,
};

/* -------------------------------------------------------------------------- */
/* construction + global backend init                                         */
/* -------------------------------------------------------------------------- */

ehem_transport *ehem_transport_default_new(const char *base_url,
                                           ehem_tls_mode tls_mode,
                                           const char *ca_file)
{
    ehem_transport *t;
    curl_state *st;
    CURL *h;

    if (base_url == NULL) {
        return NULL;
    }
    /* Make sure the process-global curl init has run (idempotent). */
    if (ehem_global_init() != EHEM_OK) {
        return NULL;
    }

    t  = calloc(1, sizeof *t);
    st = calloc(1, sizeof *st);
    h  = curl_easy_init();
    if (t == NULL || st == NULL || h == NULL) {
        if (h != NULL) {
            curl_easy_cleanup(h);
        }
        free(t);
        free(st);
        return NULL;
    }
    st->base_url = malloc(strlen(base_url) + 1);
    if (st->base_url == NULL) {
        curl_easy_cleanup(h);
        free(t);
        free(st);
        return NULL;
    }
    memcpy(st->base_url, base_url, strlen(base_url) + 1);
    st->handle = h;

    /* Per-context, non-method options set once here. */
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, st->errbuf);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);          /* thread-safe timeouts */
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);    /* device API: no redirects */
    curl_easy_setopt(h, CURLOPT_USERAGENT, "encedo-hem/" EHEM_VERSION_STRING);

    /* TLS trust mode (REQ-NET-003). */
    switch (tls_mode) {
    case EHEM_TLS_SYSTEM:
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
        break;
    case EHEM_TLS_CA_FILE:
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
        if (ca_file != NULL) {
            curl_easy_setopt(h, CURLOPT_CAINFO, ca_file);
        }
        break;
    case EHEM_TLS_INSECURE:
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
        break;
    }

    t->ops   = &CURL_OPS;
    t->state = st;
    return t;
}

ehem_rc ehem_transport_backend_global_init(void)
{
    return (curl_global_init(CURL_GLOBAL_DEFAULT) == 0) ? EHEM_OK : EHEM_ERR_DEVICE;
}

void ehem_transport_backend_global_cleanup(void)
{
    curl_global_cleanup();
}
