/**
 * test.c - libflow public API contract tests.
 * Summary: Validates each exported flow function through one dedicated test case.
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

static int signal_count;
static int signal_count_b;
static kc_flow_t *signal_ctx_seen;

/**
 * Stores one observed signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal(kc_flow_t *ctx) {
    signal_count++;
    signal_ctx_seen = ctx;
}

/**
 * Stores one observed secondary signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_flow_t *ctx) {
    signal_count_b++;
    signal_ctx_seen = ctx;
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
 * Verifies one string result.
 * @param name Check name.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
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
    return 0;
#endif
}

/**
 * Writes one fixture file into a directory.
 * @param dir Directory path.
 * @param name File name.
 * @param content File content.
 * @return 0 on success, 1 on failure.
 */
static int write_fixture(const char *dir, const char *name, const char *content) {
    char path[512];
    FILE *f;
    size_t len;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 1;
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
static int join_path(const char *dir, const char *name, char *out, size_t cap) {
    int r;

    r = snprintf(out, cap, "%s/%s", dir, name);
    return (r < 0 || (size_t)r >= cap) ? 1 : 0;
}

/**
 * Writes generated runtime fixtures.
 * @param dir Temporary directory path.
 * @return 0 on success, 1 on failure.
 */
static int write_all_fixtures(const char *dir) {
#ifdef _WIN32
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=more\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=echo default\n"
        "node.server.exec=echo <node.param.msg>\n"
        "node.install.exec=echo install\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo <flow.param.greeting> Left\n"
        "node.right.exec=echo <flow.param.greeting> Right\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#else
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=cat\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=printf \"%s\" \"default\"\n"
        "node.server.exec=printf \"%s\" \"<node.param.msg>\"\n"
        "node.install.exec=printf \"%s\" \"install\"\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=printf \"%s\" \"<flow.param.greeting> Left\"\n"
        "node.right.exec=printf \"%s\" \"<flow.param.greeting> Right\"\n") != 0)
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
 * Tests kc_flow_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_open(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open NULL out", KC_FLOW_ERROR, kc_flow_open(NULL, &opts));
    rc += expect_int("open NULL opts", KC_FLOW_ERROR, kc_flow_open(&ctx, NULL));
    rc += expect_int("open valid context", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_default(void) {
    kc_flow_options_t opts;

    opts = kc_flow_options_default();
    return expect_int("default _unused", 0, opts._unused);
}

/**
 * Tests kc_flow_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_load_env(void) {
    kc_flow_options_t opts;
    int rc;

    opts = kc_flow_options_default();
    opts._unused = 7;
    rc = 0;
    kc_flow_options_load_env(&opts);
    rc += expect_int("load_env leaves options stable", 7, opts._unused);
    kc_flow_options_load_env(NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_free(void) {
    kc_flow_options_t opts;

    opts = kc_flow_options_default();
    opts._unused = 9;
    kc_flow_options_free(&opts);
    kc_flow_options_free(NULL);
    return expect_int("options remain reusable after free", 9, opts._unused);
}

/**
 * Tests kc_flow_on_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_on_signal(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("on_signal NULL ctx", KC_FLOW_ERROR,
        kc_flow_on_signal(NULL, 10, count_signal));
    rc += expect_int("open for on_signal", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("register first handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, count_signal));
    rc += expect_int("replace existing handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, count_signal_b));
    rc += expect_int("raise replaced handler", KC_FLOW_OK,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("old handler not called", 0, signal_count);
    rc += expect_int("new handler called", 1, signal_count_b);
    rc += expect_int("remove handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, NULL));
    rc += expect_int("remove missing handler", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, NULL));
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_raise_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_raise_signal(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("raise NULL ctx", KC_FLOW_ERROR,
        kc_flow_raise_signal(NULL, 10));
    rc += expect_int("open for raise_signal", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("raise unhandled signal", KC_FLOW_ERROR,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("register signal", KC_FLOW_OK,
        kc_flow_on_signal(ctx, 10, count_signal));
    rc += expect_int("raise handled signal", KC_FLOW_OK,
        kc_flow_raise_signal(ctx, 10));
    rc += expect_int("signal callback count", 1, signal_count);
    rc += expect_true("signal callback saw context", signal_ctx_seen == ctx);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_listen_signals.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_listen_signals(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("listen_signals NULL ctx", KC_FLOW_ERROR,
        kc_flow_listen_signals(NULL));
    rc += expect_int("open for listen_signals", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("listen_signals context", KC_FLOW_OK,
        kc_flow_listen_signals(ctx));
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_listen_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_listen_signal(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("listen_signal NULL ctx", KC_FLOW_ERROR,
        kc_flow_listen_signal(NULL, SIGINT));
    rc += expect_int("open for listen_signal", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("listen one signal", KC_FLOW_OK,
        kc_flow_listen_signal(ctx, SIGINT));
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_signal_listener.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_signal_listener(void) {
    kc_flow_options_t opts;
    kc_flow_t *first;
    kc_flow_t *second;
    int rc;

    opts = kc_flow_options_default();
    first = NULL;
    second = NULL;
    rc = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("open first context", KC_FLOW_OK, kc_flow_open(&first, &opts));
    rc += expect_int("open second context", KC_FLOW_OK, kc_flow_open(&second, &opts));
    rc += expect_int("first listens globally", KC_FLOW_OK, kc_flow_listen_signals(first));
    rc += expect_int("second listens globally", KC_FLOW_OK, kc_flow_listen_signals(second));
    rc += expect_int("second signal handler", KC_FLOW_OK,
        kc_flow_on_signal(second, 77, count_signal_b));
    kc_flow_signal_listener(77);
    rc += expect_int("listener dispatches to second context", 1, signal_count_b);
    rc += expect_true("listener saw second context", signal_ctx_seen == second);
    kc_flow_close(first);
    kc_flow_close(second);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_close(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open before close", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("listen before close", KC_FLOW_OK, kc_flow_listen_signals(ctx));
    kc_flow_close(NULL);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_stop(void) {
    kc_flow_options_t opts;
    kc_flow_t *first;
    kc_flow_t *second;
    int rc;

    opts = kc_flow_options_default();
    first = NULL;
    second = NULL;
    rc = 0;
    rc += expect_int("stop NULL ctx", KC_FLOW_ERROR, kc_flow_stop(NULL));
    rc += expect_int("open first context", KC_FLOW_OK, kc_flow_open(&first, &opts));
    rc += expect_int("open second context", KC_FLOW_OK, kc_flow_open(&second, &opts));
    rc += expect_int("stop first context", KC_FLOW_OK, kc_flow_stop(first));
    rc += expect_int("stop first context again", KC_FLOW_OK, kc_flow_stop(first));
    rc += expect_int("stop second context", KC_FLOW_OK, kc_flow_stop(second));
    kc_flow_close(first);
    kc_flow_close(second);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_set(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    rc += expect_int("set NULL ctx", KC_FLOW_ERROR,
        kc_flow_set(NULL, "flow.link", "x"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("open for set", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("set NULL key", KC_FLOW_ERROR,
        kc_flow_set(ctx, NULL, "x"));
    rc += expect_int("set NULL value", KC_FLOW_ERROR,
        kc_flow_set(ctx, "flow.link", NULL));
    rc += expect_int("set invalid key", KC_FLOW_ERROR,
        kc_flow_set(ctx, "bad key", "x"));
    rc += expect_int("set overlay value", KC_FLOW_OK,
        kc_flow_set(ctx, "node.server.param.msg", "Hello"));
    rc += expect_int("set flow.link=server", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "server"));
    rc += expect_int("exec overlay after set", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("set overlay output", out, out_size, "Hello");
    kc_flow_free(out);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_unset.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_unset(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    rc += expect_int("unset NULL ctx", KC_FLOW_ERROR,
        kc_flow_unset(NULL, "flow.link"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("open for unset", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("unset NULL key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, NULL));
    rc += expect_int("unset invalid key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, "bad key"));
    rc += expect_int("unset flow.link", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    rc += expect_int("set flow.link=install", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "install"));
    rc += expect_int("exec overlay after unset", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("unset overlay output", out, out_size, "install");
    kc_flow_free(out);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_exec(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    rc += expect_int("exec NULL ctx", KC_FLOW_ERROR,
        kc_flow_exec(NULL, "x.flow", NULL, 0, &out, &out_size));
    rc += expect_int("open for exec", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("exec NULL path", KC_FLOW_ERROR,
        kc_flow_exec(ctx, NULL, NULL, 0, &out, &out_size));
    rc += expect_int("exec missing file", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, 0, &out, &out_size));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "fanout.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("exec fanout", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    rc += expect_output_contains("fanout left output", out, out_size, "Hi Left");
    rc += expect_output_contains("fanout right output", out, out_size, "Hi Right");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "stdin.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("exec stdin", KC_FLOW_OK,
        kc_flow_exec(ctx, path, "Pipe Input", 10, &out, &out_size));
    rc += expect_output_contains("stdin output", out, out_size, "Pipe Input");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "cycle.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("exec cycle fails", KC_FLOW_ERROR,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_exec_entry.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_exec_entry(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    rc += expect_int("open for exec_entry", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_int("exec_entry NULL ctx", KC_FLOW_ERROR,
        kc_flow_exec_entry(NULL, path, "default", NULL, 0, &out, &out_size));
    rc += expect_int("exec_entry empty entry", KC_FLOW_ERROR,
        kc_flow_exec_entry(ctx, path, "", NULL, 0, &out, &out_size));
    rc += expect_int("exec_entry default", KC_FLOW_OK,
        kc_flow_exec_entry(ctx, path, "default", NULL, 0, &out, &out_size));
    rc += expect_output_contains("entry default output", out, out_size, "default");
    kc_flow_free(out);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_free(void) {
    char *output;

    output = (char *)malloc(8);
    if (output == NULL) return 1;
    memcpy(output, "owned", 6);
    kc_flow_free(output);
    kc_flow_free(NULL);
    return 0;
}

/**
 * Tests kc_flow_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_version(void) {
    return expect_true("version is non-zero", kc_flow_version() != 0U);
}

/**
 * Tests kc_flow_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_strerror(void) {
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char *out;
    size_t out_size;
    const char *err;
    int rc;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    rc = 0;
    rc += expect_string("strerror NULL ctx", "unknown error", kc_flow_strerror(NULL));
    rc += expect_int("open for strerror", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    rc += expect_string("strerror fresh ctx", "unknown error", kc_flow_strerror(ctx));
    rc += expect_int("exec missing file for strerror", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, 0, &out, &out_size));
    err = kc_flow_strerror(ctx);
    rc += expect_true("strerror after failure is non-NULL", err != NULL);
    rc += expect_true("strerror after failure is descriptive",
        err != NULL && strcmp(err, "unknown error") != 0);
    kc_flow_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs one public API contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "kc_flow_open") == 0) return case_kc_flow_open();
    if (strcmp(argv[1], "kc_flow_options_default") == 0) {
        return case_kc_flow_options_default();
    }
    if (strcmp(argv[1], "kc_flow_options_load_env") == 0) {
        return case_kc_flow_options_load_env();
    }
    if (strcmp(argv[1], "kc_flow_options_free") == 0) {
        return case_kc_flow_options_free();
    }
    if (strcmp(argv[1], "kc_flow_on_signal") == 0) return case_kc_flow_on_signal();
    if (strcmp(argv[1], "kc_flow_raise_signal") == 0) return case_kc_flow_raise_signal();
    if (strcmp(argv[1], "kc_flow_listen_signals") == 0) {
        return case_kc_flow_listen_signals();
    }
    if (strcmp(argv[1], "kc_flow_listen_signal") == 0) {
        return case_kc_flow_listen_signal();
    }
    if (strcmp(argv[1], "kc_flow_signal_listener") == 0) {
        return case_kc_flow_signal_listener();
    }
    if (strcmp(argv[1], "kc_flow_close") == 0) return case_kc_flow_close();
    if (strcmp(argv[1], "kc_flow_stop") == 0) return case_kc_flow_stop();
    if (strcmp(argv[1], "kc_flow_set") == 0) return case_kc_flow_set();
    if (strcmp(argv[1], "kc_flow_unset") == 0) return case_kc_flow_unset();
    if (strcmp(argv[1], "kc_flow_exec") == 0) return case_kc_flow_exec();
    if (strcmp(argv[1], "kc_flow_exec_entry") == 0) return case_kc_flow_exec_entry();
    if (strcmp(argv[1], "kc_flow_free") == 0) return case_kc_flow_free();
    if (strcmp(argv[1], "kc_flow_version") == 0) return case_kc_flow_version();
    if (strcmp(argv[1], "kc_flow_strerror") == 0) return case_kc_flow_strerror();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
