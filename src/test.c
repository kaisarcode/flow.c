/**
 * test.c - libflow portable contract tests.
 * Summary: Validates exported libflow behavior through the public C API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "flow.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef FLOW_TEST_ETC_DIR
#define FLOW_TEST_ETC_DIR "."
#endif

static int signal_count = 0;
static int signal_count_b = 0;
static kc_flow_t *signal_ctx_seen = NULL;

/**
 * Stores one observed signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal(kc_flow_t *ctx) {
    if (ctx != NULL) {
        signal_count++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Stores one observed secondary signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_flow_t *ctx) {
    if (ctx != NULL) {
        signal_count_b++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Verifies one integer result.
 * @param name Check name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean result.
 * @param name Check name.
 * @param condition Condition expected to be true.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        fprintf(stderr, "%s: expected true, got false\n", name);
        return 1;
    }
    return 0;
}

/**
 * Creates one temporary directory for generated flow fixtures.
 * @param out Buffer to receive the path.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int make_temp_dir(char *out, size_t cap) {
    const char *base;
    int r;

#ifdef _WIN32
    base = getenv("TEMP");
    if (base == NULL || *base == '\0') base = ".";
    {
        int i;
        for (i = 0; i < 100; i++) {
            r = snprintf(out, cap, "%s/flow-test-%ld-%d", base,
                (long)_getpid(), i);
            if (r < 0 || (size_t)r >= cap) return 1;
            if (_mkdir(out) == 0) return 0;
            if (errno != EEXIST) return 1;
        }
    }
    return 1;
#else
    base = getenv("TMPDIR");
    if (base == NULL || *base == '\0') base = "/tmp";
    r = snprintf(out, cap, "%s/flow-test-XXXXXX", base);
    if (r < 0 || (size_t)r >= cap) return 1;
    if (mkdtemp(out) == NULL) return 1;
#endif
    return 0;
}

/**
 * Writes one fixture file into a directory.
 * @param dir Directory path.
 * @param name File name.
 * @param content File content.
 * @return 0 on success, 1 on failure.
 */
static int write_fixture(const char *dir, const char *name,
    const char *content)
{
    char path[512];
    FILE *f;
    size_t len;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >=
        (int)sizeof(path)) return 1;
    f = fopen(path, "w");
    if (f == NULL) return 1;
    len = strlen(content);
    if (fwrite(content, 1, len, f) != len) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/**
 * Joins a directory and file name into a path buffer.
 * @param dir Directory path.
 * @param name File name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int join_path(const char *dir, const char *name,
    char *out, size_t cap)
{
    int r;

    r = snprintf(out, cap, "%s/%s", dir, name);
    return (r < 0 || (size_t)r >= cap) ? 1 : 0;
}

/**
 * Writes all generated flow fixtures for the exec test cases.
 * @param dir Temporary directory path.
 * @return 0 on success, 1 on failure.
 */
static int write_all_fixtures(const char *dir) {
#ifdef _WIN32
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo <flow.param.greeting> Left\n"
        "node.right.exec=echo <flow.param.greeting> Right\n") != 0)
        return 1;
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=more\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "flow.owner=team\n"
        "node.default.exec=echo default\n"
        "node.server.exec=echo <node.param.msg>\n"
        "node.install.exec=echo install\n"
        "node.left.exec=echo left\n"
        "node.right.exec=echo right\n") != 0)
        return 1;
    if (write_fixture(dir, "heredoc.flow",
        "flow.link=root\n"
        "node.root.exec=<<CMD\n"
        "echo heredoc\n"
        "CMD\n") != 0)
        return 1;
    if (write_fixture(dir, "comment.flow",
        "# full-line comment\n"
        "flow.link=root\n"
        "# another comment\n"
        "node.root.exec=echo comment-ok\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#else
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=printf \"%s\" \"<flow.param.greeting> Left\"\n"
        "node.right.exec=printf \"%s\" \"<flow.param.greeting> Right\"\n") != 0)
        return 1;
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=cat\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "flow.owner=team\n"
        "node.default.exec=printf \"%s\" \"default\"\n"
        "node.server.exec=printf \"%s\" \"<node.param.msg>\"\n"
        "node.install.exec=printf \"%s\" \"install\"\n"
        "node.left.exec=printf \"%s\" \"left\"\n"
        "node.right.exec=printf \"%s\" \"right\"\n") != 0)
        return 1;
    if (write_fixture(dir, "heredoc.flow",
        "flow.link=root\n"
        "node.root.exec=<<CMD\n"
        "printf \"%s\" \"heredoc\"\n"
        "CMD\n") != 0)
        return 1;
    if (write_fixture(dir, "comment.flow",
        "# full-line comment\n"
        "flow.link=root\n"
        "# another comment\n"
        "node.root.exec=printf \"%s\" \"comment-ok\"\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#endif
    return 0;
}

/**
 * Verifies that output contains a substring anywhere in the buffer.
 * @param name Check name.
 * @param output Output buffer.
 * @param output_size Output size.
 * @param needle Expected substring.
 * @return 0 on success, 1 on failure.
 */
static int expect_output_contains(const char *name,
    const char *output, size_t output_size, const char *needle)
{
    size_t needle_len;
    size_t i;

    if (output == NULL || output_size == 0) {
        fprintf(stderr, "%s: expected output containing '%s', got empty\n",
            name, needle);
        return 1;
    }
    needle_len = strlen(needle);
    if (output_size < needle_len) {
        fprintf(stderr, "%s: expected output containing '%s', got '%.*s'\n",
            name, needle, (int)output_size, output);
        return 1;
    }
    for (i = 0; i + needle_len <= output_size; i++) {
        if (memcmp(output + i, needle, needle_len) == 0) return 0;
    }
    fprintf(stderr, "%s: expected output containing '%s', got '%.*s'\n",
        name, needle, (int)output_size, output);
    return 1;
}

/**
 * Verifies the build version API.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    if (kc_flow_version() == 0U) {
        fprintf(stderr, "version: expected non-zero build timestamp, got 0\n");
        return 1;
    }
    return 0;
}

/**
 * Verifies options defaults, environment loading, and cleanup.
 * @return 0 on success, 1 on failure.
 */
static int case_options(void) {
    kc_flow_options_t opts;
    int rc;

    opts = kc_flow_options_default();
    rc = 0;
    kc_flow_options_load_env(&opts);
    kc_flow_options_free(&opts);
    kc_flow_options_load_env(NULL);
    kc_flow_options_free(NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies configuration-related API behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_options_contract(void) {
    int rc;

    rc = 0;
    rc += case_version();
    rc += case_options();
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies context lifecycle and NULL guard contracts.
 * @return 0 on success, 1 on failure.
 */
static int case_lifecycle(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open(NULL, opts)", KC_FLOW_ERROR,
        kc_flow_open(NULL, &opts));
    rc += expect_int("open(out, NULL)", KC_FLOW_ERROR,
        kc_flow_open(&ctx, NULL));
    rc += expect_int("open(out, opts)", KC_FLOW_OK,
        kc_flow_open(&ctx, &opts));
    rc += expect_int("stop(NULL)", KC_FLOW_ERROR, kc_flow_stop(NULL));
    rc += expect_int("stop(ctx)", KC_FLOW_OK, kc_flow_stop(ctx));
    kc_flow_close(NULL);
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies signal callback registration and context routing contracts.
 * @return 0 on success, 1 on failure.
 */
static int case_signals(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    kc_flow_t *second;
    int rc;
    int i;

    opts = kc_flow_options_default();
    ctx = NULL;
    second = NULL;
    rc = 0;
    signal_count = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("on_signal(NULL)", KC_FLOW_ERROR,
        kc_flow_on_signal(NULL, 10, count_signal));
    rc += expect_int("raise_signal(NULL)", KC_FLOW_ERROR,
        kc_flow_raise_signal(NULL, 10));
    rc += expect_int("listen_signals(NULL)", KC_FLOW_ERROR,
        kc_flow_listen_signals(NULL));
    rc += expect_int("listen_signal(NULL)", KC_FLOW_ERROR,
        kc_flow_listen_signal(NULL, SIGINT));
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("raise unhandled signal", KC_FLOW_ERROR,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("register signal handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, count_signal));
    rc += expect_int("raise handled signal", KC_FLOW_OK,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("signal callback count", 1, signal_count);
    rc += expect_true("signal callback saw correct ctx", signal_ctx_seen == ctx);
    rc += expect_int("replace signal handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, count_signal_b));
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("raise replaced signal", KC_FLOW_OK,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("old signal handler not called after replace", 0,
        signal_count);
    rc += expect_int("new signal handler called after replace", 1,
        signal_count_b);
    rc += expect_int("remove signal handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, NULL));
    rc += expect_int("raise removed signal", KC_FLOW_ERROR,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("remove absent signal is OK", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, NULL));
    for (i = 0; i < 10; i++) {
        rc += expect_int("register growth signal", KC_FLOW_OK,
            kc_flow_on_signal(ctx, 100 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise last growth signal", KC_FLOW_OK,
        kc_flow_raise_signal(ctx, 109));
    rc += expect_int("growth callback count", 1, signal_count);
    rc += expect_int("listen signals", KC_FLOW_OK,
        kc_flow_listen_signals(ctx));
    rc += expect_int("listen one OS signal", KC_FLOW_OK,
        kc_flow_listen_signal(ctx, SIGINT));
    rc += expect_int("open second", KC_FLOW_OK, kc_flow_open(&second, &opts));
    rc += expect_int("second listens globally", KC_FLOW_OK,
        kc_flow_listen_signals(second));
    rc += expect_int("second signal handler", KC_FLOW_OK,
        kc_flow_on_signal(second, 77, count_signal_b));
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    kc_flow_signal_listener(77);
    rc += expect_int("listener dispatches to second context", 1,
        signal_count_b);
    rc += expect_true("listener dispatch saw second ctx", signal_ctx_seen == second);
    kc_flow_close(ctx);
    kc_flow_close(second);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies context-owned stop state isolation across two contexts.
 * @return 0 on success, 1 on failure.
 */
static int case_multictx_stop(void) {
    kc_flow_options_t opts;
    kc_flow_t *first;
    kc_flow_t *second;
    int rc;

    opts = kc_flow_options_default();
    first = NULL;
    second = NULL;
    rc = 0;
    rc += expect_int("open first", KC_FLOW_OK, kc_flow_open(&first, &opts));
    rc += expect_int("open second", KC_FLOW_OK, kc_flow_open(&second, &opts));
    rc += expect_int("stop first", KC_FLOW_OK, kc_flow_stop(first));
    rc += expect_int("stop second", KC_FLOW_OK, kc_flow_stop(second));
    rc += expect_int("stop first again", KC_FLOW_OK, kc_flow_stop(first));
    rc += expect_int("stop(NULL)", KC_FLOW_ERROR, kc_flow_stop(NULL));
    kc_flow_close(first);
    kc_flow_close(second);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies overlay set and unset NULL guard contracts.
 * @return 0 on success, 1 on failure.
 */
static int case_overlay_guards(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("set(NULL)", KC_FLOW_ERROR,
        kc_flow_set(NULL, "flow.link", "x"));
    rc += expect_int("unset(NULL)", KC_FLOW_ERROR,
        kc_flow_unset(NULL, "flow.link"));
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("set(ctx, NULL)", KC_FLOW_ERROR,
        kc_flow_set(ctx, NULL, "x"));
    rc += expect_int("set(ctx, key, NULL)", KC_FLOW_ERROR,
        kc_flow_set(ctx, "flow.link", NULL));
    rc += expect_int("unset(ctx, NULL)", KC_FLOW_ERROR,
        kc_flow_unset(ctx, NULL));
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec on a missing file returns an error and sets strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_error(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("exec(NULL)", KC_FLOW_ERROR,
        kc_flow_exec(NULL, "x.flow", NULL, 0, &out, &out_size));
    rc += expect_int("exec(ctx, NULL)", KC_FLOW_ERROR,
        kc_flow_exec(ctx, NULL, NULL, 0, &out, &out_size));
    rc += expect_int("exec missing file", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, 0, &out, &out_size));
    if (kc_flow_strerror(ctx) == NULL) {
        fprintf(stderr, "strerror: expected non-NULL after error\n");
        rc++;
    }
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec on the page.flow fixture with stdin input.
 * @return 0 on success, 1 on failure.
 */
#ifndef _WIN32
static int case_exec_page(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char path[512];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    if (snprintf(path, sizeof(path), "%s/page.flow", FLOW_TEST_ETC_DIR) >=
        (int)sizeof(path)) return 1;
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("set flow.heading=Welcome", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.heading", "Welcome"));
    rc += expect_int("set flow.slug=welcome", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.slug", "welcome"));
    rc += expect_int("exec page.flow with stdin", KC_FLOW_OK,
        kc_flow_exec(ctx, path, "Hello from stdin", 16, &out, &out_size));
    rc += expect_output_contains("page title", out, out_size,
        "# Tiny Flow Site / Welcome");
    rc += expect_output_contains("page body", out, out_size,
        "Hello from stdin");
    kc_flow_free(out);
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec on the site.flow fixture with default path.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_site(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char path[512];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    if (snprintf(path, sizeof(path), "%s/site.flow", FLOW_TEST_ETC_DIR) >=
        (int)sizeof(path)) return 1;
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("exec site.flow default", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("site robots output", out, out_size,
        "# Tiny Flow Site / Robots");
    kc_flow_free(out);
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec on site.flow with overlay set for path routing.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_site_overlay(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char path[512];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    if (snprintf(path, sizeof(path), "%s/site.flow", FLOW_TEST_ETC_DIR) >=
        (int)sizeof(path)) return 1;
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("set flow.path=/", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.path", "/"));
    rc += expect_int("exec site.flow home route", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("home route output", out, out_size,
        "# Tiny Flow Site / Home");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    rc += expect_int("set flow.path=/missing", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.path", "/missing"));
    rc += expect_int("exec site.flow missing route", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("missing route output", out, out_size,
        "# Tiny Flow Site / Missing");
    rc += expect_output_contains("missing 404", out, out_size,
        "404 NOT_FOUND");
    kc_flow_free(out);
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}
#endif

static int case_exec_fixtures(void);

/**
 * Verifies the bundled etc flow examples as one runtime contract.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_examples(void) {
    int rc;

    rc = 0;
#ifdef _WIN32
    rc += case_exec_fixtures();
#else
    rc += case_exec_page();
    rc += case_exec_site();
    rc += case_exec_site_overlay();
#endif
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec_entry on site.flow with an explicit entry node.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_entry(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char path[512];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
#ifdef _WIN32
    {
        char tmpdir[320];
        if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
        if (write_all_fixtures(tmpdir) != 0) return 1;
        if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    }
#else
    if (snprintf(path, sizeof(path), "%s/site.flow", FLOW_TEST_ETC_DIR) >=
        (int)sizeof(path)) return 1;
#endif
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("exec_entry(NULL)", KC_FLOW_ERROR,
        kc_flow_exec_entry(NULL, path, "request", NULL, 0, &out, &out_size));
#ifdef _WIN32
    rc += expect_int("exec_entry default", KC_FLOW_OK,
        kc_flow_exec_entry(ctx, path, "default", NULL, 0, &out, &out_size));
    rc += expect_output_contains("entry default output", out, out_size,
        "default");
#else
    rc += expect_int("exec_entry robots", KC_FLOW_OK,
        kc_flow_exec_entry(ctx, path, "robots", NULL, 0, &out, &out_size));
    rc += expect_output_contains("entry robots output", out, out_size,
        "# Tiny Flow Site / Robots");
#endif
    kc_flow_free(out);
    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies exec on generated fixture files covering runtime behaviors.
 * @return 0 on success, 1 on failure.
 */
static int case_exec_fixtures(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char fpath[640];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "exec-fixtures: failed to create temp dir\n");
        return 1;
    }
    if (write_all_fixtures(tmpdir) != 0) {
        fprintf(stderr, "exec-fixtures: failed to write fixtures\n");
        return 1;
    }
    rc += expect_int("open", KC_FLOW_OK, kc_flow_open(&ctx, &opts));

    if (join_path(tmpdir, "fanout.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("exec fanout", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));
    rc += expect_output_contains("fanout left output", out, out_size,
        "Hi Left");
    rc += expect_output_contains("fanout right output", out, out_size,
        "Hi Right");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "heredoc.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("exec heredoc", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));
    rc += expect_output_contains("heredoc output", out, out_size, "heredoc");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "comment.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("exec comment", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));
    rc += expect_output_contains("comment output", out, out_size, "comment-ok");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "stdin.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("exec stdin", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, "Pipe Input", 10, &out, &out_size));
    rc += expect_output_contains("stdin output", out, out_size, "Pipe Input");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "cycle.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("exec cycle should fail", KC_FLOW_ERROR,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));

    kc_flow_close(ctx);
    ctx = NULL;
    rc += expect_int("open for overlay", KC_FLOW_OK,
        kc_flow_open(&ctx, &opts));

    if (join_path(tmpdir, "overlay.flow", fpath, sizeof(fpath)) != 0) return 1;
    rc += expect_int("set overlay server msg", KC_FLOW_OK,
        kc_flow_set(ctx, "node.server.param.msg", "Hello"));
    rc += expect_int("unset flow.link", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    rc += expect_int("set flow.link=server", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "server"));
    rc += expect_int("exec overlay server", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));
    rc += expect_output_contains("overlay server output", out, out_size,
        "Hello");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    rc += expect_int("unset flow.link again", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    rc += expect_int("set flow.link=install", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "install"));
    rc += expect_int("exec overlay install", KC_FLOW_OK,
        kc_flow_exec(ctx, fpath, NULL, 0, &out, &out_size));
    rc += expect_output_contains("overlay install output", out, out_size,
        "install");
    kc_flow_free(out);

    kc_flow_close(ctx);
    kc_flow_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs one libflow contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "options") == 0) return case_options_contract();
    if (strcmp(argv[1], "lifecycle") == 0) return case_lifecycle();
    if (strcmp(argv[1], "signals") == 0) return case_signals();
    if (strcmp(argv[1], "multictx-stop") == 0) return case_multictx_stop();
    if (strcmp(argv[1], "overlay-guards") == 0) return case_overlay_guards();
    if (strcmp(argv[1], "exec-error") == 0) return case_exec_error();
    if (strcmp(argv[1], "exec-examples") == 0) return case_exec_examples();
    if (strcmp(argv[1], "exec-entry") == 0) return case_exec_entry();
    if (strcmp(argv[1], "exec-fixtures") == 0) return case_exec_fixtures();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
