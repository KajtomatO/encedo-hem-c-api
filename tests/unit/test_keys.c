/*
 * test_keys.c — hem-tool `keys` support: the protected-key classifier and the
 * `keys list` subcommand, driven offline through the fake transport.
 *
 * verifies: REQ-TOOL-005 (label-only protected classification: exact TLS labels,
 *           near-misses unprotected, (Android)/(iPhone) any case/position), and
 *           REQ-TOOL-004 (keys list: full-repo walk prints every key once with
 *           [PROTECTED] marks + summary counts; read-only = auth + list only;
 *           missing passphrase → exit 2; auth failure → exit 1).
 *
 * The tool code under test (src/tools/hem-tool/keys.c via hem-tool-core) uses
 * ONLY the public API; the auth exchange is driven with the internal clock/KDF
 * seams (proto_auth.h) like test_cert_install. Output is captured via a portable
 * tmpfile() (not open_memstream — the M2-070 MinGW lesson).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "keys.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push_login(ehem_transport *fake)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"kl\"}",
                 (long long)(EJWT_FX_NOW + 100000));
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* -------------------------------------------------------------------------- */
/* classifier (REQ-TOOL-005)                                                  */
/* -------------------------------------------------------------------------- */

static void test_classifier_table(void **state)
{
    (void)state;
    struct { const char *label; bool protected; } cases[] = {
        /* exact TLS material → protected */
        { "TLS PrivateKey",       true },
        { "TLS Certificate",      true },
        /* near-misses → NOT protected */
        { "tls privatekey",       false },   /* case differs; exact match only */
        { "TLS PrivateKey 2",     false },   /* not exact */
        { "TLS Certificate ",     false },   /* trailing space */
        { "TLS",                  false },
        /* (Android)/(iPhone) substrings, any case/position → protected */
        { "SM-S938B (Android)",   true },
        { "(android) tail",       true },
        { "MID(ANDROID)DLE",      true },
        { "My (iPhone)",          true },
        { "(IPHONE)",             true },
        { "x(iPhOnE)y",           true },
        /* ordinary labels → NOT protected */
        { "it-ecdh-1",            false },
        { "Encedo OIDC - tomasz", false },
        { "android",              false },   /* no parens */
        { "iphone user",          false },   /* no parens */
        { "",                     false },
    };
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (hem_key_is_protected(cases[i].label) != cases[i].protected) {
            fail_msg("classifier: '%s' expected %s",
                     cases[i].label, cases[i].protected ? "protected" : "unprotected");
        }
    }
    assert_false(hem_key_is_protected(NULL));
}

/* -------------------------------------------------------------------------- */
/* keys list (REQ-TOOL-004)                                                   */
/* -------------------------------------------------------------------------- */

typedef struct { const char *kid; const char *label; const char *type; } kspec;

static void push_key_page(ehem_transport *fake, int total, int offset,
                          const kspec *ks, int n)
{
    char buf[2048];
    int off = snprintf(buf, sizeof buf,
                       "{\"offset\":%d,\"total\":%d,\"listed\":%d,\"list\":[",
                       offset, total, n);
    int i;
    for (i = 0; i < n; i++) {
        off += snprintf(buf + off, sizeof buf - (size_t)off,
                        "%s{\"kid\":\"%s\",\"label\":\"%s\",\"type\":\"%s\"}",
                        i == 0 ? "" : ",", ks[i].kid, ks[i].label, ks[i].type);
    }
    snprintf(buf + off, sizeof buf - (size_t)off, "]}");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, buf), 0);
}

/* Count non-overlapping occurrences of `needle` in `hay`. */
static int count_substr(const char *hay, const char *needle)
{
    int c = 0;
    size_t nlen = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        c++;
        p += nlen;
    }
    return c;
}

static void test_keys_list_marks_and_counts(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    /* Page 1 of 2 (12 keys total, device caps at 15; the walk pages by 10):
     * three protected (both TLS labels + an Android phone), seven ordinary. */
    const kspec page1[] = {
        { "00000000000000000000000000000000", "TLS PrivateKey",  "PKEY,GENERIC_DER" },
        { "00000000000000000000000000000001", "TLS Certificate", "CERT,GENERIC_DER" },
        { "00000000000000000000000000000002", "Pixel 8 (Android)", "ECDH,CURVE25519" },
        { "00000000000000000000000000000003", "it-key-3",  "ED25519" },
        { "00000000000000000000000000000004", "it-key-4",  "ED25519" },
        { "00000000000000000000000000000005", "it-key-5",  "ED25519" },
        { "00000000000000000000000000000006", "it-key-6",  "ED25519" },
        { "00000000000000000000000000000007", "it-key-7",  "ED25519" },
        { "00000000000000000000000000000008", "it-key-8",  "ED25519" },
        { "00000000000000000000000000000009", "it-key-9",  "ED25519" },
    };
    const kspec page2[] = {
        { "00000000000000000000000000000010", "it-key-10", "ED25519" },
        { "00000000000000000000000000000011", "it-key-11", "ED25519" },
    };

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);                              /* req 0,1 */
    push_key_page(fake, 12, 0, page1, 10);         /* req 2 */
    push_key_page(fake, 12, 10, page2, 2);         /* req 3 */

    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    assert_non_null(out);
    assert_non_null(errf);

    hem_keys_opts ko;
    memset(&ko, 0, sizeof ko);
    ko.passphrase = EJWT_FX_PASSPHRASE;
    ko.out = out;
    ko.err = errf;

    assert_int_equal(hem_keys_list_run(ctx, &ko), HEM_KEYS_OK);

    /* Read the captured listing back. */
    char content[4096];
    rewind(out);
    size_t n = fread(content, 1, sizeof content - 1, out);
    content[n] = '\0';

    /* Every key printed once (12 kids), exactly the 3 protected marks, summary.
     * Each key line starts "  <kid>"; all kids share a 28-zero prefix. */
    assert_int_equal(count_substr(content, "  0000000000000000000000000000"), 12);
    assert_int_equal(count_substr(content, "[PROTECTED]"), 3);
    assert_non_null(strstr(content, "'TLS PrivateKey'"));
    assert_non_null(strstr(content, "'Pixel 8 (Android)'"));
    assert_non_null(strstr(content, "12 key(s), 3 protected"));
    /* An ordinary key is present and its line is unmarked: the only 3 marks are
     * the protected ones (asserted above), so it-key-9 carries none. */
    assert_non_null(strstr(content, "'it-key-9'"));

    /* Read-only: login (2) + two list pages (2) — nothing else. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);

    fclose(out);
    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* No passphrase → HEM_KEYS_USAGE (2), and NO network traffic. */
static void test_keys_list_no_passphrase(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    FILE *errf = tmpfile();
    assert_non_null(errf);

    hem_keys_opts ko;
    memset(&ko, 0, sizeof ko);
    ko.passphrase = NULL;
    ko.out = errf;   /* unused */
    ko.err = errf;

    assert_int_equal(hem_keys_list_run(ctx, &ko), HEM_KEYS_USAGE);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Auth failure (token POST rejected 401) → HEM_KEYS_RUNTIME (1). */
static void test_keys_list_auth_failure(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* challenge OK, then the token POST is rejected. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 401,
        "{\"error\":\"bad passphrase\"}"), 0);

    ehem_ctx *ctx = ctx_with(fake);
    FILE *errf = tmpfile();
    assert_non_null(errf);

    hem_keys_opts ko;
    memset(&ko, 0, sizeof ko);
    ko.passphrase = EJWT_FX_PASSPHRASE;
    ko.out = errf;
    ko.err = errf;

    assert_int_equal(hem_keys_list_run(ctx, &ko), HEM_KEYS_RUNTIME);

    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_classifier_table),
        cmocka_unit_test(test_keys_list_marks_and_counts),
        cmocka_unit_test(test_keys_list_no_passphrase),
        cmocka_unit_test(test_keys_list_auth_failure),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
