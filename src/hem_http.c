#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

#include "hem/hem.h"
#include "internal.h"

/* -------------------------------------------------------------------------
 * libcurl write callback -- accumulates response into ctx->resp_buf
 * ---------------------------------------------------------------------- */
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    hem_ctx_t *ctx = (hem_ctx_t *)userdata;
    size_t total = size * nmemb;

    /* Grow buffer if needed */
    if (ctx->resp_len + total + 1 > ctx->resp_cap) {
        size_t new_cap = ctx->resp_cap ? ctx->resp_cap * 2 : 4096;
        while (new_cap < ctx->resp_len + total + 1)
            new_cap *= 2;
        char *new_buf = realloc(ctx->resp_buf, new_cap);
        if (!new_buf) return 0; /* causes CURLE_WRITE_ERROR */
        ctx->resp_buf = new_buf;
        ctx->resp_cap = new_cap;
    }

    memcpy(ctx->resp_buf + ctx->resp_len, ptr, total);
    ctx->resp_len += total;
    ctx->resp_buf[ctx->resp_len] = '\0';
    return total;
}

/* -------------------------------------------------------------------------
 * Shared curl setup applied before every request
 * ---------------------------------------------------------------------- */
static void apply_common_opts(hem_ctx_t *ctx, const char *token,
                               struct curl_slist **headers_out)
{
    /* Reset response buffer */
    ctx->resp_len = 0;
    if (ctx->resp_buf) ctx->resp_buf[0] = '\0';
    ctx->http_status = 0;

    /* Build headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    if (token && token[0] != '\0') {
        char auth[2200];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth);
    }

    curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, ctx);

    /* TLS: skip verification -- device authenticity handled by check-in */
    curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYHOST, 0L);

    /*
     * Do not reuse connections across requests.
     * The HEM device (embedded HTTP server) closes connections between
     * requests; reuse causes CURLE_SEND_ERROR on the second POST.
     */
    curl_easy_setopt(ctx->curl, CURLOPT_FORBID_REUSE, 1L);

    /* Timeout: 30s connect, 60s total */
    curl_easy_setopt(ctx->curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(ctx->curl, CURLOPT_TIMEOUT, 60L);

    *headers_out = headers;
}

static hem_error_t finish_request(hem_ctx_t *ctx, CURLcode res,
                                   struct curl_slist *headers)
{
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        hem_set_error(ctx, HEM_ERR_HTTP, curl_easy_strerror(res));
        return HEM_ERR_HTTP;
    }

    long http_code = 0;
    curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &http_code);
    ctx->http_status = (int)http_code;

    if (http_code == 409) {
        hem_set_error(ctx, HEM_ERR_DEVICE_FAILURE, "device in failure state (FLS)");
        return HEM_ERR_DEVICE_FAILURE;
    }
    if (http_code == 401 || http_code == 403) {
        hem_set_error(ctx, HEM_ERR_AUTH, "authentication or scope error");
        return HEM_ERR_AUTH;
    }
    if (http_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "HTTP %ld", http_code);
        hem_set_error(ctx, HEM_ERR_HTTP_STATUS, msg);
        return HEM_ERR_HTTP_STATUS;
    }

    ctx->last_error = HEM_OK;
    return HEM_OK;
}

static void build_url(const hem_ctx_t *ctx, const char *path,
                       char *url_out, size_t url_size)
{
    snprintf(url_out, url_size, "%s%s", ctx->base_url, path);
}

/* -------------------------------------------------------------------------
 * Public HTTP functions
 * ---------------------------------------------------------------------- */

hem_error_t hem_http_get(hem_ctx_t *ctx, const char *path, const char *token)
{
    char url[512];
    build_url(ctx, path, url, sizeof(url));

    struct curl_slist *headers = NULL;
    apply_common_opts(ctx, token, &headers);

    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_HTTPGET, 1L);

    CURLcode res = curl_easy_perform(ctx->curl);
    return finish_request(ctx, res, headers);
}

hem_error_t hem_http_post(hem_ctx_t *ctx, const char *path,
                           const char *body, const char *token)
{
    char url[512];
    build_url(ctx, path, url, sizeof(url));

    struct curl_slist *headers = NULL;
    apply_common_opts(ctx, token, &headers);

    const char *post_body = body ? body : "{}";
    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, post_body);
    curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post_body));

    CURLcode res = curl_easy_perform(ctx->curl);
    return finish_request(ctx, res, headers);
}

hem_error_t hem_http_delete(hem_ctx_t *ctx, const char *path, const char *token)
{
    char url[512];
    build_url(ctx, path, url, sizeof(url));

    struct curl_slist *headers = NULL;
    apply_common_opts(ctx, token, &headers);

    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    CURLcode res = curl_easy_perform(ctx->curl);

    /* Reset custom request so next GET/POST works correctly */
    curl_easy_setopt(ctx->curl, CURLOPT_CUSTOMREQUEST, NULL);

    return finish_request(ctx, res, headers);
}

hem_error_t hem_http_post_url(hem_ctx_t *ctx, const char *url, const char *body)
{
    struct curl_slist *headers = NULL;
    apply_common_opts(ctx, NULL, &headers);

    const char *post_body = body ? body : "{}";
    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, post_body);
    curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post_body));

    CURLcode res = curl_easy_perform(ctx->curl);
    return finish_request(ctx, res, headers);
}
