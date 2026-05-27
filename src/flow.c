/**
 * flow.c - Flow command line interface.
 * Summary: Runs flat branch-oriented flow documents from the shell.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#define KC_FLOW_VERSION "1.1.1"

/**
 * Read standard input into memory.
 * @param output Output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_cli_read_stdin(char **output, size_t *output_size) {
    char *data = NULL;
    size_t size = 0;
    size_t cap = 0;
    char chunk[4096];
    size_t n;

    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (size + n + 1 > cap) {
            char *next;
            cap = cap ? cap * 2 : 4096;
            while (cap < size + n + 1) {
                cap *= 2;
            }
            next = (char *)realloc(data, cap);
            if (!next) {
                free(data);
                return KC_FLOW_ERROR;
            }
            data = next;
        }
        memcpy(data + size, chunk, n);
        size += n;
    }
    if (ferror(stdin)) {
        free(data);
        return KC_FLOW_ERROR;
    }
    if (!data) {
        data = (char *)malloc(1);
        if (!data) {
            return KC_FLOW_ERROR;
        }
    }
    data[size] = '\0';
    *output = data;
    *output_size = size;
    return KC_FLOW_OK;
}

/**
 * Read standard input when the process receives piped data.
 * @param output Output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_cli_read_input(char **output, size_t *output_size) {
#ifndef _WIN32
    if (isatty(fileno(stdin))) {
        char *empty;

        empty = (char *)malloc(1U);
        if (empty == NULL) {
            return KC_FLOW_ERROR;
        }

        empty[0] = '\0';
        *output = empty;
        *output_size = 0U;
        return KC_FLOW_OK;
    }
#endif

    return kc_flow_cli_read_stdin(output, output_size);
}

/**
 * Parse one strict positive integer.
 * @param text Source text.
 * @param value Output value pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_cli_parse_workers(const char *text, size_t *value) {
    size_t out = 0;
    const char *cursor = text;

    if (!text || !*text) {
        return KC_FLOW_ERROR;
    }
    while (*cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return KC_FLOW_ERROR;
        }
        out = out * 10 + (size_t)(*cursor - '0');
        cursor++;
    }
    if (out == 0) {
        return KC_FLOW_ERROR;
    }
    *value = out;
    return KC_FLOW_OK;
}

/**
 * Print command help.
 * @param name Program name.
 * @return None.
 */
static void kc_flow_cli_help(const char *name) {
    printf("Usage: %s file.flow [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --link <name>      Execute one explicit entry node\n");
    printf("    --set key=value    Append one overlay record\n");
    printf("    --unset <key>      Remove prior records for one key\n");
    printf("    --workers <n>      Set worker count hint\n");
    printf("    -h, --help         Show this help message\n");
    printf("    -v, --version      Show version\n");
}

/**
 * Print command version.
 * @return None.
 */
static void kc_flow_cli_version(void) {
    printf("flow %s\n", KC_FLOW_VERSION);
}

/**
 * Print one command failure.
 * @param message Error message.
 * @return Process failure status.
 */
static int kc_flow_cli_fail(const char *message) {
    fprintf(stderr, "flow: %s\n", message);
    return 1;
}

/**
 * Program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_flow_t *ctx;
    const char *run_path = NULL;
    const char *entry = NULL;
    char *input = NULL;
    char *output = NULL;
    size_t input_size = 0;
    size_t output_size = 0;
    int i;
    int rc;

    if (argc == 1) {
        kc_flow_cli_help(argv[0]);
        return 1;
    }
    ctx = kc_flow_open();
    if (!ctx) {
        return kc_flow_cli_fail("out of memory");
    }
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_flow_cli_help(argv[0]);
            kc_flow_close(ctx);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_flow_cli_version();
            kc_flow_close(ctx);
            return 0;
        }
        if (strcmp(argv[i], "--link") == 0) {
            if (++i >= argc) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("missing value for --link");
            }
            entry = argv[i];
        } else if (strcmp(argv[i], "--set") == 0) {
            char key[256];
            const char *eq;
            size_t key_size;
            if (++i >= argc) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("missing value for --set");
            }
            eq = strchr(argv[i], '=');
            if (!eq || eq == argv[i]) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("invalid --set value");
            }
            key_size = (size_t)(eq - argv[i]);
            if (key_size >= sizeof(key)) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("overlay key too long");
            }
            memcpy(key, argv[i], key_size);
            key[key_size] = '\0';
            if (kc_flow_set(ctx, key, eq + 1) != KC_FLOW_OK) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("invalid --set overlay");
            }
        } else if (strcmp(argv[i], "--unset") == 0) {
            if (++i >= argc) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("missing value for --unset");
            }
            if (kc_flow_unset(ctx, argv[i]) != KC_FLOW_OK) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("invalid --unset overlay");
            }
        } else if (strcmp(argv[i], "--workers") == 0) {
            size_t workers;
            if (++i >= argc || kc_flow_cli_parse_workers(argv[i], &workers) != KC_FLOW_OK || kc_flow_set_workers(ctx, workers) != KC_FLOW_OK) {
                kc_flow_close(ctx);
                return kc_flow_cli_fail("invalid worker count");
            }
        } else if (argv[i][0] == '-') {
            kc_flow_close(ctx);
            return kc_flow_cli_fail("unknown option");
        } else if (run_path == NULL) {
            run_path = argv[i];
        } else {
            kc_flow_close(ctx);
            return kc_flow_cli_fail("unexpected positional argument");
        }
    }
    if (!run_path) {
        kc_flow_close(ctx);
        return kc_flow_cli_fail("missing flow file");
    }
    if (kc_flow_cli_read_input(&input, &input_size) != KC_FLOW_OK) {
        kc_flow_close(ctx);
        return kc_flow_cli_fail("unable to read stdin");
    }
    rc = entry ? kc_flow_exec_entry(ctx, run_path, entry, input, input_size, &output, &output_size) : kc_flow_exec(ctx, run_path, input, input_size, &output, &output_size);
    free(input);
    if (rc != KC_FLOW_OK) {
        fprintf(stderr, "flow: %s\n", kc_flow_strerror(ctx));
        kc_flow_close(ctx);
        return 1;
    }
    if (output_size > 0 && fwrite(output, 1, output_size, stdout) != output_size) {
        kc_flow_free(output);
        kc_flow_close(ctx);
        return kc_flow_cli_fail("unable to write stdout");
    }
    kc_flow_free(output);
    kc_flow_close(ctx);
    return 0;
}
