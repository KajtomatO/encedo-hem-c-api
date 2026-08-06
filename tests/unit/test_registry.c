/*
 * test_registry.c — the hem-tool command registry, help rendering, and
 * device-URL resolution (hem-tool-core, no network).
 *
 * verifies: REQ-TOOL-017 (resolution precedence flag > env > default;
 *           the one-line stderr notice only when the default is used),
 *           REQ-TOOL-019 (every command carries an auth class; the
 *           grouped listing places each command under its class;
 *           registry completeness — a command without a class/synopsis/
 *           summary/details fails here),
 *           REQ-TOOL-020 (per-command help at both family depths shows
 *           only that command's options; the top page shows none of
 *           them and stays one screen; unknown names are refused)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "registry.h"

static char *slurp(FILE *f)
{
    long n;
    char *buf;
    assert_int_equal(fseek(f, 0, SEEK_END), 0);
    n = ftell(f);
    assert_true(n >= 0);
    rewind(f);
    buf = malloc((size_t)n + 1);
    assert_non_null(buf);
    assert_int_equal(fread(buf, 1, (size_t)n, f), (size_t)n);
    buf[n] = '\0';
    return buf;
}

static char *render_top(void)
{
    FILE *f = tmpfile();
    char *s;
    assert_non_null(f);
    hem_help_top(f, "test");
    s = slurp(f);
    fclose(f);
    return s;
}

static char *render_cmd(const char *cmd, const char *sub, int expect_rc)
{
    FILE *f = tmpfile();
    char *s;
    assert_non_null(f);
    assert_int_equal(hem_help_command(f, cmd, sub), expect_rc);
    s = slurp(f);
    fclose(f);
    return s;
}

/* REQ-TOOL-019: registry completeness — every command fully described. */
static void test_registry_complete(void **state)
{
    (void)state;
    size_t n = 0, i;
    const hem_command *cmds = hem_registry_commands(&n);
    assert_non_null(cmds);
    assert_true(n >= 19);   /* the 1.0 command set */
    for (i = 0; i < n; i++) {
        const hem_command *c = &cmds[i];
        assert_non_null(c->name);
        assert_true(c->name[0] != '\0');
        assert_non_null(c->synopsis);
        assert_true(c->synopsis[0] != '\0');
        assert_non_null(c->summary);
        assert_true(c->summary[0] != '\0');
        assert_non_null(c->details);
        assert_true(c->details[0] != '\0');
        assert_in_range(c->auth, HEM_AUTH_NONE, HEM_AUTH_PASSPHRASE_ONLY);
        if (c->option_count > 0) {
            assert_non_null(c->options);
        }
    }
}

/* REQ-TOOL-019: the auth classes match what the implementation enforces
 * (behavior pinned in test_tool_auth.c: keys list works with either
 * credential = BEARER; ext pair rejects --mobile = PASSPHRASE_ONLY;
 * status/checkin never log in = NONE). */
static void test_auth_classes(void **state)
{
    (void)state;
    assert_int_equal(hem_registry_find("status", NULL)->auth, HEM_AUTH_NONE);
    assert_int_equal(hem_registry_find("checkin", NULL)->auth, HEM_AUTH_NONE);
    assert_int_equal(hem_registry_find("keys", "list")->auth, HEM_AUTH_BEARER);
    assert_int_equal(hem_registry_find("sign", NULL)->auth, HEM_AUTH_BEARER);
    assert_int_equal(hem_registry_find("ext", "pair")->auth,
                     HEM_AUTH_PASSPHRASE_ONLY);
    /* Exactly the three documented groups exist in the top page, in
     * none → bearer → passphrase-only order. */
    char *top = render_top();
    const char *g1 = strstr(top, "NO credentials");
    const char *g2 = strstr(top, "passphrase or --mobile:");
    const char *g3 = strstr(top, "PASSPHRASE (the device demands sub=\"U\")");
    assert_non_null(g1);
    assert_non_null(g2);
    assert_non_null(g3);
    assert_true(g1 < g2 && g2 < g3);
    /* status sits in the first group; ext pair after the third header. */
    assert_true(strstr(top, "status") > g1);
    assert_true(strstr(g3, "ext pair") != NULL);
    free(top);
}

/* REQ-TOOL-020: the top page carries NO command-specific options and
 * stays around one screen. */
static void test_top_page_shape(void **state)
{
    (void)state;
    char *top = render_top();
    size_t lines = 0;
    const char *p;
    assert_null(strstr(top, "--sigctx"));   /* sign-only */
    assert_null(strstr(top, "--wait"));     /* reboot-only */
    assert_null(strstr(top, "--label-prefix"));
    assert_non_null(strstr(top, "--mobile"));  /* shared options ARE here */
    assert_non_null(strstr(top, HEM_TOOL_DEFAULT_URL));
    assert_non_null(strstr(top, "help <command>"));
    for (p = top; *p != '\0'; p++) {
        lines += (*p == '\n');
    }
    assert_true(lines <= 45);
    free(top);
}

/* REQ-TOOL-020: per-command help shows only that command's options, at
 * both family depths; unknown names refused. */
static void test_per_command_help(void **state)
{
    (void)state;
    char *s;

    s = render_cmd("sign", NULL, 0);
    assert_non_null(strstr(s, "--sigctx"));
    assert_non_null(strstr(s, "any bearer"));
    assert_null(strstr(s, "--wait"));
    free(s);

    s = render_cmd("reboot", NULL, 0);
    assert_non_null(strstr(s, "--wait"));
    assert_null(strstr(s, "--sigctx"));
    free(s);

    /* Family summary at depth 1, command help at depth 2. */
    s = render_cmd("keys", NULL, 0);
    assert_non_null(strstr(s, "keys rm"));
    assert_non_null(strstr(s, "subcommands"));
    free(s);

    s = render_cmd("keys", "rm", 0);
    assert_non_null(strstr(s, "--label-prefix"));
    assert_non_null(strstr(s, "YES"));
    free(s);

    s = render_cmd("ext", "pair", 0);
    assert_non_null(strstr(s, "passphrase only"));
    free(s);

    /* Unknown command / subcommand → -1 (main.c exits 2 on that). */
    s = render_cmd("nosuch", NULL, -1);
    free(s);
    s = render_cmd("keys", "nosuch", -1);
    free(s);
}

/* REQ-TOOL-017: precedence + the notice fires only for the default. */
static void test_url_resolution(void **state)
{
    (void)state;
    FILE *err = tmpfile();
    char *msg;
    assert_non_null(err);

    assert_string_equal(hem_tool_resolve_url("https://a", "https://b", err),
                        "https://a");
    assert_string_equal(hem_tool_resolve_url(NULL, "https://b", err),
                        "https://b");
    assert_string_equal(hem_tool_resolve_url("", "https://b", err),
                        "https://b");
    msg = slurp(err);
    assert_string_equal(msg, "");   /* explicit sources → NO notice */
    free(msg);

    assert_string_equal(hem_tool_resolve_url(NULL, NULL, err),
                        HEM_TOOL_DEFAULT_URL);
    msg = slurp(err);
    assert_non_null(strstr(msg, HEM_TOOL_DEFAULT_URL));
    assert_non_null(strstr(msg, "notice"));
    free(msg);

    fclose(err);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_registry_complete),
        cmocka_unit_test(test_auth_classes),
        cmocka_unit_test(test_top_page_shape),
        cmocka_unit_test(test_per_command_help),
        cmocka_unit_test(test_url_resolution),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
