/**
 * libflow.c - Branch-oriented flow runtime implementation.
 * Summary: Parses flow documents and executes independent data branches.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "flow.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#define KC_FLOW_MAX_RECORDS 2048
#define KC_FLOW_MAX_NODES 512
#define KC_FLOW_MAX_BRANCHES 2048
#define KC_FLOW_MAX_KEY 256
#define KC_FLOW_MAX_PATH 4096
#define KC_FLOW_ERROR_SIZE 512

typedef enum kc_flow_overlay_kind {
    KC_FLOW_OVERLAY_SET = 1,
    KC_FLOW_OVERLAY_UNSET = 2
} kc_flow_overlay_kind_t;

typedef struct kc_flow_record {
    char *key;
    char *value;
} kc_flow_record_t;

typedef struct kc_flow_store {
    kc_flow_record_t items[KC_FLOW_MAX_RECORDS];
    size_t count;
} kc_flow_store_t;

typedef struct kc_flow_links {
    char *items[KC_FLOW_MAX_RECORDS];
    size_t count;
} kc_flow_links_t;

typedef struct kc_flow_node {
    char *ref;
    char *file;
    char *exec;
    kc_flow_store_t params;
    kc_flow_store_t meta;
    kc_flow_links_t links;
} kc_flow_node_t;

typedef struct kc_flow_model {
    char *id;
    kc_flow_store_t params;
    kc_flow_store_t meta;
    kc_flow_links_t entries;
    kc_flow_node_t nodes[KC_FLOW_MAX_NODES];
    size_t node_count;
} kc_flow_model_t;

typedef struct kc_flow_overlay {
    kc_flow_overlay_kind_t kind;
    char *key;
    char *value;
} kc_flow_overlay_t;

typedef struct kc_flow_branch {
    char *data;
    size_t size;
} kc_flow_branch_t;

typedef struct kc_flow_branches {
    kc_flow_branch_t items[KC_FLOW_MAX_BRANCHES];
    size_t count;
} kc_flow_branches_t;

struct kc_flow {
    kc_flow_overlay_t overlays[KC_FLOW_MAX_RECORDS];
    size_t overlay_count;
    size_t workers;
    char error[KC_FLOW_ERROR_SIZE];
};

/**
 * Duplicate one string.
 * @param text Source text.
 * @return Heap copy or NULL.
 */
static char *kc_flow_dup(const char *text) {
    size_t size;
    char *copy;

    if (!text) {
        return NULL;
    }
    size = strlen(text) + 1;
    copy = (char *)malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

/**
 * Duplicate one byte buffer and add a trailing null byte.
 * @param data Source bytes.
 * @param size Source size.
 * @return Heap copy or NULL.
 */
static char *kc_flow_dup_bytes(const void *data, size_t size) {
    char *copy;

    copy = (char *)malloc(size + 1);
    if (!copy) {
        return NULL;
    }
    if (size > 0 && data) {
        memcpy(copy, data, size);
    }
    copy[size] = '\0';
    return copy;
}

/**
 * Store one formatted context error.
 * @param ctx Context pointer.
 * @param message Error message.
 * @return KC_FLOW_ERROR.
 */
static int kc_flow_fail(kc_flow_t *ctx, const char *message) {
    if (ctx) {
        snprintf(ctx->error, sizeof(ctx->error), "%s", message);
    }
    return KC_FLOW_ERROR;
}

/**
 * Initialize one key-value store.
 * @param store Store pointer.
 * @return None.
 */
static void kc_flow_store_init(kc_flow_store_t *store) {
    memset(store, 0, sizeof(*store));
}

/**
 * Release one key-value store.
 * @param store Store pointer.
 * @return None.
 */
static void kc_flow_store_free(kc_flow_store_t *store) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        free(store->items[i].key);
        free(store->items[i].value);
    }
    kc_flow_store_init(store);
}

/**
 * Resolve one key from a store.
 * @param store Store pointer.
 * @param key Lookup key.
 * @return Stored value or NULL.
 */
static const char *kc_flow_store_get(const kc_flow_store_t *store, const char *key) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].key, key) == 0) {
            return store->items[i].value;
        }
    }
    return NULL;
}

/**
 * Add or replace one store value.
 * @param store Store pointer.
 * @param key Record key.
 * @param value Record value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_store_set(kc_flow_store_t *store, const char *key, const char *value) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].key, key) == 0) {
            char *next = kc_flow_dup(value);
            if (!next) {
                return KC_FLOW_ERROR;
            }
            free(store->items[i].value);
            store->items[i].value = next;
            return KC_FLOW_OK;
        }
    }
    if (store->count >= KC_FLOW_MAX_RECORDS) {
        return KC_FLOW_ERROR;
    }
    store->items[store->count].key = kc_flow_dup(key);
    store->items[store->count].value = kc_flow_dup(value);
    if (!store->items[store->count].key || !store->items[store->count].value) {
        free(store->items[store->count].key);
        free(store->items[store->count].value);
        return KC_FLOW_ERROR;
    }
    store->count++;
    return KC_FLOW_OK;
}

/**
 * Copy one store into another store.
 * @param dst Destination store.
 * @param src Source store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_store_copy(kc_flow_store_t *dst, const kc_flow_store_t *src) {
    size_t i;

    kc_flow_store_init(dst);
    for (i = 0; i < src->count; ++i) {
        if (kc_flow_store_set(dst, src->items[i].key, src->items[i].value) != KC_FLOW_OK) {
            kc_flow_store_free(dst);
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one link list.
 * @param links Link list pointer.
 * @return None.
 */
static void kc_flow_links_init(kc_flow_links_t *links) {
    memset(links, 0, sizeof(*links));
}

/**
 * Release one link list.
 * @param links Link list pointer.
 * @return None.
 */
static void kc_flow_links_free(kc_flow_links_t *links) {
    size_t i;

    for (i = 0; i < links->count; ++i) {
        free(links->items[i]);
    }
    kc_flow_links_init(links);
}

/**
 * Append one link value.
 * @param links Link list pointer.
 * @param value Link value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_links_add(kc_flow_links_t *links, const char *value) {
    if (links->count >= KC_FLOW_MAX_RECORDS) {
        return KC_FLOW_ERROR;
    }
    links->items[links->count] = kc_flow_dup(value);
    if (!links->items[links->count]) {
        return KC_FLOW_ERROR;
    }
    links->count++;
    return KC_FLOW_OK;
}

/**
 * Trim ASCII whitespace in place.
 * @param text Mutable text.
 * @return Trimmed text pointer.
 */
static char *kc_flow_trim(char *text) {
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

/**
 * Read one line from a stream.
 * @param fp Stream pointer.
 * @param line Line buffer pointer.
 * @param cap Buffer capacity pointer.
 * @return 1 when a line was read, otherwise 0.
 */
static int kc_flow_read_line(FILE *fp, char **line, size_t *cap) {
    size_t len = 0;
    int ch;

    if (!*line) {
        *cap = 256;
        *line = (char *)malloc(*cap);
        if (!*line) {
            return 0;
        }
    }
    while ((ch = fgetc(fp)) != EOF) {
        if (len + 2 > *cap) {
            char *next;
            *cap *= 2;
            next = (char *)realloc(*line, *cap);
            if (!next) {
                free(*line);
                *line = NULL;
                return 0;
            }
            *line = next;
        }
        (*line)[len++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (len == 0 && ch == EOF) {
        return 0;
    }
    (*line)[len] = '\0';
    return 1;
}

/**
 * Check whether one key segment is structurally valid.
 * @param text Segment pointer.
 * @param len Segment length.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_part_valid(const char *text, size_t len) {
    size_t i;

    if (len == 0) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Check whether one dotted key suffix is valid.
 * @param text Suffix text.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_tail_valid(const char *text) {
    const char *part = text;
    const char *cur = text;

    if (!text || !*text) {
        return 0;
    }
    while (*cur) {
        if (*cur == '.') {
            if (!kc_flow_key_part_valid(part, (size_t)(cur - part))) {
                return 0;
            }
            part = cur + 1;
        }
        cur++;
    }
    return kc_flow_key_part_valid(part, (size_t)(cur - part));
}

/**
 * Check whether one full document key is valid.
 * @param key Full key.
 * @return 1 when valid, otherwise 0.
 */
static int kc_flow_key_valid(const char *key) {
    if (strncmp(key, "flow.", 5) == 0) {
        return kc_flow_key_tail_valid(key + 5);
    }
    if (strncmp(key, "node.", 5) == 0) {
        const char *ref = key + 5;
        const char *field = strchr(ref, '.');
        return field && kc_flow_key_part_valid(ref, (size_t)(field - ref)) && kc_flow_key_tail_valid(field + 1);
    }
    return 0;
}

/**
 * Add one raw document record.
 * @param records Document records.
 * @param key Record key.
 * @param value Record value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_records_add(kc_flow_store_t *records, const char *key, const char *value) {
    if (records->count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return KC_FLOW_ERROR;
    }
    records->items[records->count].key = kc_flow_dup(key);
    records->items[records->count].value = kc_flow_dup(value);
    if (!records->items[records->count].key || !records->items[records->count].value) {
        free(records->items[records->count].key);
        free(records->items[records->count].value);
        return KC_FLOW_ERROR;
    }
    records->count++;
    return KC_FLOW_OK;
}

/**
 * Remove all exact records matching one key.
 * @param records Document records.
 * @param key Record key.
 * @return None.
 */
static void kc_flow_records_remove(kc_flow_store_t *records, const char *key) {
    size_t i = 0;

    while (i < records->count) {
        if (strcmp(records->items[i].key, key) == 0) {
            size_t j;
            free(records->items[i].key);
            free(records->items[i].value);
            for (j = i + 1; j < records->count; ++j) {
                records->items[j - 1] = records->items[j];
            }
            records->count--;
            memset(&records->items[records->count], 0, sizeof(records->items[records->count]));
            continue;
        }
        i++;
    }
}

/**
 * Read one heredoc value from a flow file.
 * @param fp Stream pointer.
 * @param marker End marker.
 * @param out_value Output value pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_read_heredoc(FILE *fp, const char *marker, char **out_value) {
    char *line = NULL;
    char *value = NULL;
    size_t cap = 0;
    size_t size = 0;
    size_t value_cap = 0;

    while (kc_flow_read_line(fp, &line, &cap)) {
        char *check = kc_flow_trim(line);
        size_t len;
        if (strcmp(check, marker) == 0) {
            free(line);
            if (!value) {
                value = kc_flow_dup("");
            }
            *out_value = value;
            return value ? KC_FLOW_OK : KC_FLOW_ERROR;
        }
        len = strlen(line);
        if (size + len + 1 > value_cap) {
            char *next;
            value_cap = value_cap ? value_cap * 2 : 256;
            while (value_cap < size + len + 1) {
                value_cap *= 2;
            }
            next = (char *)realloc(value, value_cap);
            if (!next) {
                free(line);
                free(value);
                return KC_FLOW_ERROR;
            }
            value = next;
        }
        memcpy(value + size, line, len);
        size += len;
        value[size] = '\0';
    }
    free(line);
    free(value);
    return KC_FLOW_ERROR;
}

/**
 * Read flow file records.
 * @param path Flow file path.
 * @param records Output document records.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_read_records(const char *path, kc_flow_store_t *records) {
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;

    kc_flow_store_init(records);
    fp = fopen(path, "r");
    if (!fp) {
        return KC_FLOW_ERROR;
    }
    while (kc_flow_read_line(fp, &line, &cap)) {
        char *key = kc_flow_trim(line);
        char *eq;
        char *value;
        char *owned = NULL;
        if (!*key || *key == '#') {
            continue;
        }
        eq = strchr(key, '=');
        if (!eq || eq == key) {
            free(line);
            fclose(fp);
            kc_flow_store_free(records);
            return KC_FLOW_ERROR;
        }
        *eq = '\0';
        value = kc_flow_trim(eq + 1);
        key = kc_flow_trim(key);
        if (strncmp(value, "<<", 2) == 0) {
            if (kc_flow_read_heredoc(fp, kc_flow_trim(value + 2), &owned) != KC_FLOW_OK) {
                free(line);
                fclose(fp);
                kc_flow_store_free(records);
                return KC_FLOW_ERROR;
            }
            value = owned;
        }
        if (kc_flow_records_add(records, key, value) != KC_FLOW_OK) {
            free(owned);
            free(line);
            fclose(fp);
            kc_flow_store_free(records);
            return KC_FLOW_ERROR;
        }
        free(owned);
    }
    free(line);
    fclose(fp);
    return KC_FLOW_OK;
}

/**
 * Apply context overlays to document records.
 * @param ctx Context pointer.
 * @param records Mutable records.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_apply_overlays(kc_flow_t *ctx, kc_flow_store_t *records) {
    size_t i;

    for (i = 0; i < ctx->overlay_count; ++i) {
        if (!kc_flow_key_valid(ctx->overlays[i].key)) {
            return kc_flow_fail(ctx, "invalid structural key");
        }
        if (ctx->overlays[i].kind == KC_FLOW_OVERLAY_UNSET) {
            kc_flow_records_remove(records, ctx->overlays[i].key);
        } else if (kc_flow_records_add(records, ctx->overlays[i].key, ctx->overlays[i].value) != KC_FLOW_OK) {
            return kc_flow_fail(ctx, "unable to apply overlay");
        }
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one parsed model.
 * @param model Model pointer.
 * @return None.
 */
static void kc_flow_model_init(kc_flow_model_t *model) {
    memset(model, 0, sizeof(*model));
}

/**
 * Release one parsed model.
 * @param model Model pointer.
 * @return None.
 */
static void kc_flow_model_free(kc_flow_model_t *model) {
    size_t i;

    free(model->id);
    kc_flow_store_free(&model->params);
    kc_flow_store_free(&model->meta);
    kc_flow_links_free(&model->entries);
    for (i = 0; i < model->node_count; ++i) {
        free(model->nodes[i].ref);
        free(model->nodes[i].file);
        free(model->nodes[i].exec);
        kc_flow_store_free(&model->nodes[i].params);
        kc_flow_store_free(&model->nodes[i].meta);
        kc_flow_links_free(&model->nodes[i].links);
    }
    kc_flow_model_init(model);
}

/**
 * Find one parsed node.
 * @param model Model pointer.
 * @param ref Node reference.
 * @return Node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_find(kc_flow_model_t *model, const char *ref) {
    size_t i;

    for (i = 0; i < model->node_count; ++i) {
        if (strcmp(model->nodes[i].ref, ref) == 0) {
            return &model->nodes[i];
        }
    }
    return NULL;
}

/**
 * Resolve or create one parsed node.
 * @param model Model pointer.
 * @param ref Node reference.
 * @return Node pointer or NULL.
 */
static kc_flow_node_t *kc_flow_model_node(kc_flow_model_t *model, const char *ref) {
    kc_flow_node_t *node;

    node = kc_flow_model_find(model, ref);
    if (node) {
        return node;
    }
    if (model->node_count >= KC_FLOW_MAX_NODES) {
        return NULL;
    }
    node = &model->nodes[model->node_count++];
    memset(node, 0, sizeof(*node));
    node->ref = kc_flow_dup(ref);
    if (!node->ref) {
        return NULL;
    }
    return node;
}

/**
 * Parse one node-scoped record.
 * @param model Model pointer.
 * @param key Record key.
 * @param value Record value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_node(kc_flow_model_t *model, const char *key, const char *value) {
    const char *ref_start = key + 5;
    const char *field = strchr(ref_start, '.');
    char ref[KC_FLOW_MAX_KEY];
    kc_flow_node_t *node;
    size_t len;

    if (!field || field == ref_start) {
        return KC_FLOW_ERROR;
    }
    len = (size_t)(field - ref_start);
    if (len >= sizeof(ref)) {
        return KC_FLOW_ERROR;
    }
    memcpy(ref, ref_start, len);
    ref[len] = '\0';
    node = kc_flow_model_node(model, ref);
    if (!node) {
        return KC_FLOW_ERROR;
    }
    field++;
    if (strcmp(field, "file") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(node->file);
        node->file = next;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "exec") == 0) {
        char *next = kc_flow_dup(value);
        if (!next) {
            return KC_FLOW_ERROR;
        }
        free(node->exec);
        node->exec = next;
        return KC_FLOW_OK;
    }
    if (strcmp(field, "link") == 0) {
        return kc_flow_links_add(&node->links, value);
    }
    if (strncmp(field, "param.", 6) == 0) {
        return kc_flow_store_set(&node->params, field + 6, value);
    }
    return kc_flow_store_set(&node->meta, field, value);
}

/**
 * Parse effective records into one model.
 * @param records Document records.
 * @param model Output model.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_parse_model(const kc_flow_store_t *records, kc_flow_model_t *model) {
    size_t i;

    kc_flow_model_init(model);
    for (i = 0; i < records->count; ++i) {
        const char *key = records->items[i].key;
        const char *value = records->items[i].value;
        if (strcmp(key, "flow.id") == 0) {
            char *next = kc_flow_dup(value);
            if (!next) {
                return KC_FLOW_ERROR;
            }
            free(model->id);
            model->id = next;
        } else if (strcmp(key, "flow.link") == 0) {
            if (kc_flow_links_add(&model->entries, value) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "flow.param.", 11) == 0) {
            if (kc_flow_store_set(&model->params, key + 11, value) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "node.", 5) == 0) {
            if (kc_flow_parse_node(model, key, value) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else if (strncmp(key, "flow.", 5) == 0) {
            if (kc_flow_store_set(&model->meta, key + 5, value) != KC_FLOW_OK) {
                return KC_FLOW_ERROR;
            }
        } else {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Walk one node for validation.
 * @param model Model pointer.
 * @param node Node pointer.
 * @param seen Seen markers.
 * @param stack Stack markers.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_validate_walk(kc_flow_model_t *model, kc_flow_node_t *node, unsigned char *seen, unsigned char *stack) {
    size_t i;
    size_t index = (size_t)(node - model->nodes);

    if (stack[index]) {
        return KC_FLOW_ERROR;
    }
    if (seen[index]) {
        return KC_FLOW_OK;
    }
    seen[index] = 1;
    stack[index] = 1;
    for (i = 0; i < node->links.count; ++i) {
        kc_flow_node_t *child = kc_flow_model_find(model, node->links.items[i]);
        if (!child || kc_flow_validate_walk(model, child, seen, stack) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
    }
    stack[index] = 0;
    return KC_FLOW_OK;
}

/**
 * Validate targets and cycles in one model.
 * @param model Model pointer.
 * @param entry Optional explicit entry.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_validate_model(kc_flow_model_t *model, const char *entry) {
    unsigned char seen[KC_FLOW_MAX_NODES];
    unsigned char stack[KC_FLOW_MAX_NODES];
    size_t i;

    memset(seen, 0, sizeof(seen));
    memset(stack, 0, sizeof(stack));
    if (entry) {
        kc_flow_node_t *node = kc_flow_model_find(model, entry);
        return node ? kc_flow_validate_walk(model, node, seen, stack) : KC_FLOW_ERROR;
    }
    for (i = 0; i < model->entries.count; ++i) {
        kc_flow_node_t *node = kc_flow_model_find(model, model->entries.items[i]);
        if (!node || kc_flow_validate_walk(model, node, seen, stack) != KC_FLOW_OK) {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}

/**
 * Build one path relative to another file.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param base_file Base file path.
 * @param value Target value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_relative_path(char *out, size_t out_size, const char *base_file, const char *value) {
    const char *slash;
    size_t dir_len;

    if (!value || !*value) {
        return KC_FLOW_ERROR;
    }
    if (value[0] == '/'
#ifdef _WIN32
        || (isalpha((unsigned char)value[0]) && value[1] == ':')
#endif
    ) {
        return snprintf(out, out_size, "%s", value) >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    slash = strrchr(base_file, '/');
    if (!slash) {
        return snprintf(out, out_size, "%s", value) >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    dir_len = (size_t)(slash - base_file);
    if (dir_len + 1 + strlen(value) + 1 > out_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(out, base_file, dir_len);
    out[dir_len] = '/';
    strcpy(out + dir_len + 1, value);
    return KC_FLOW_OK;
}

/**
 * Build one directory string from a file path.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param file File path.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
#ifndef _WIN32
static int kc_flow_dir(char *out, size_t out_size, const char *file) {
    const char *slash = strrchr(file, '/');
    size_t len;

    if (!slash) {
        return snprintf(out, out_size, ".") >= (int)out_size ? KC_FLOW_ERROR : KC_FLOW_OK;
    }
    len = (size_t)(slash - file);
    if (len == 0) {
        len = 1;
    }
    if (len >= out_size) {
        return KC_FLOW_ERROR;
    }
    memcpy(out, file, len);
    out[len] = '\0';
    return KC_FLOW_OK;
}
#endif

/**
 * Resolve one placeholder value.
 * @param name Placeholder name.
 * @param flow Flow parameters.
 * @param node Node parameters.
 * @return Value pointer or NULL.
 */
static const char *kc_flow_placeholder(const char *name, const kc_flow_store_t *flow, const kc_flow_store_t *node) {
    if (strncmp(name, "flow.param.", 11) == 0) {
        return kc_flow_store_get(flow, name + 11);
    }
    if (strncmp(name, "node.param.", 11) == 0) {
        return kc_flow_store_get(node, name + 11);
    }
    return NULL;
}

/**
 * Resolve placeholders in one template.
 * @param text Template text.
 * @param flow Flow parameters.
 * @param node Node parameters.
 * @return Resolved heap string or NULL.
 */
static char *kc_flow_template(const char *text, const kc_flow_store_t *flow, const kc_flow_store_t *node) {
    char *out;
    size_t cap;
    size_t len = 0;
    size_t i;

    if (!text) {
        return NULL;
    }
    cap = strlen(text) + 1;
    out = (char *)malloc(cap);
    if (!out) {
        return NULL;
    }
    for (i = 0; text[i]; ++i) {
        if (text[i] == '<') {
            char name[KC_FLOW_MAX_KEY];
            const char *value;
            size_t end = i + 1;
            size_t name_len;
            int balance = 1;
            while (text[end] && balance > 0) {
                if (text[end] == '<') balance++;
                else if (text[end] == '>') balance--;
                if (balance > 0) end++;
            }
            if (text[end] != '>') {
                free(out);
                return NULL;
            }
            name_len = end - i - 1;
            if (name_len == 0 || name_len >= sizeof(name)) {
                free(out);
                return NULL;
            }
            memcpy(name, text + i + 1, name_len);
            name[name_len] = '\0';
            {
                char *res_name = kc_flow_template(name, flow, node);
                value = kc_flow_placeholder(res_name ? res_name : name, flow, node);
                free(res_name);
            }
            if (!value) {
                free(out);
                return NULL;
            }
            while (len + strlen(value) + 1 > cap) {
                char *next;
                cap *= 2;
                next = (char *)realloc(out, cap);
                if (!next) {
                    free(out);
                    return NULL;
                }
                out = next;
            }
            memcpy(out + len, value, strlen(value));
            len += strlen(value);
            i = end;
        } else {
            if (len + 2 > cap) {
                char *next;
                cap *= 2;
                next = (char *)realloc(out, cap);
                if (!next) {
                    free(out);
                    return NULL;
                }
                out = next;
            }
            out[len++] = text[i];
        }
    }
    out[len] = '\0';
    return out;
}

/**
 * Collect one node parameter scope.
 * @param flow Flow parameters.
 * @param node Node pointer.
 * @param out Output parameter store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_collect_node(const kc_flow_store_t *flow, const kc_flow_node_t *node, kc_flow_store_t *out) {
    size_t i;

    kc_flow_store_init(out);
    for (i = 0; i < node->params.count; ++i) {
        char *value = kc_flow_template(node->params.items[i].value, flow, out);
        if (!value) {
            kc_flow_store_free(out);
            return KC_FLOW_ERROR;
        }
        if (kc_flow_store_set(out, node->params.items[i].key, value) != KC_FLOW_OK) {
            free(value);
            kc_flow_store_free(out);
            return KC_FLOW_ERROR;
        }
        free(value);
    }
    return KC_FLOW_OK;
}

/**
 * Initialize one branch list.
 * @param branches Branch list pointer.
 * @return None.
 */
static void kc_flow_branches_init(kc_flow_branches_t *branches) {
    memset(branches, 0, sizeof(*branches));
}

/**
 * Release one branch list.
 * @param branches Branch list pointer.
 * @return None.
 */
static void kc_flow_branches_free(kc_flow_branches_t *branches) {
    size_t i;

    for (i = 0; i < branches->count; ++i) {
        free(branches->items[i].data);
    }
    kc_flow_branches_init(branches);
}

/**
 * Append one owned branch.
 * @param branches Branch list pointer.
 * @param data Owned data pointer.
 * @param size Data size.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_branches_take(kc_flow_branches_t *branches, char *data, size_t size) {
    if (branches->count >= KC_FLOW_MAX_BRANCHES) {
        free(data);
        return KC_FLOW_ERROR;
    }
    branches->items[branches->count].data = data;
    branches->items[branches->count].size = size;
    branches->count++;
    return KC_FLOW_OK;
}

/**
 * Append one copied branch.
 * @param branches Branch list pointer.
 * @param data Source data.
 * @param size Source size.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_branches_copy(kc_flow_branches_t *branches, const void *data, size_t size) {
    char *copy = kc_flow_dup_bytes(data, size);

    if (!copy) {
        return KC_FLOW_ERROR;
    }
    return kc_flow_branches_take(branches, copy, size);
}

#ifndef _WIN32
/**
 * Add scoped parameters to the process environment.
 * @param prefix Environment prefix.
 * @param store Parameter store.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_set_env_store(const char *prefix, const kc_flow_store_t *store) {
    size_t i;

    for (i = 0; i < store->count; ++i) {
        char name[KC_FLOW_MAX_KEY];
        size_t j;
        int n = snprintf(name, sizeof(name), "%s", prefix);
        if (n < 0 || (size_t)n >= sizeof(name)) {
            return KC_FLOW_ERROR;
        }
        for (j = 0; store->items[i].key[j] && (size_t)n + 1 < sizeof(name); ++j) {
            unsigned char ch = (unsigned char)store->items[i].key[j];
            name[n++] = (char)(isalnum(ch) ? toupper(ch) : '_');
        }
        name[n] = '\0';
        if (setenv(name, store->items[i].value, 1) != 0) {
            return KC_FLOW_ERROR;
        }
    }
    return KC_FLOW_OK;
}
#endif

/**
 * Parse a command string into an argv array.
 * @param command Command string.
 * @param out_argc Output argument count.
 * @return Null-terminated array of strings, or NULL.
 */
static char **kc_flow_parse_argv(const char *command, int *out_argc) {
    int argc = 0;
    int cap = 8;
    char **argv = (char **)malloc(cap * sizeof(char *));
    const char *p = command;
    if (!argv) return NULL;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc + 1 >= cap) {
            char **next;
            cap *= 2;
            next = (char **)realloc(argv, cap * sizeof(char *));
            if (!next) {
                int i;
                for (i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                return NULL;
            }
            argv = next;
        }
        if (*p == '"') {
            const char *start = ++p;
            while (*p && *p != '"') p++;
            argv[argc++] = kc_flow_dup_bytes(start, (size_t)(p - start));
            if (*p == '"') p++;
        } else {
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            argv[argc++] = kc_flow_dup_bytes(start, (size_t)(p - start));
        }
    }
    argv[argc] = NULL;
    *out_argc = argc;
    return argv;
}

/**
 * Release an argv array.
 * @param argv Null-terminated array pointer.
 * @return None.
 */
static void kc_flow_free_argv(char **argv) {
    int i;
    if (!argv) return;
    for (i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

/**
 * Execute a built-in command internally.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param in Input stream.
 * @param out Output stream.
 * @return KC_FLOW_OK on success, KC_FLOW_ERROR, or 1 if not a builtin.
 */
static int kc_flow_run_builtin(int argc, char **argv, FILE *in, FILE *out) {
    if (argc == 0) return 1;
    if (strcmp(argv[0], "echo") == 0) {
        int i;
        for (i = 1; i < argc; i++) {
            fprintf(out, "%s%s", argv[i], (i + 1 < argc) ? " " : "");
        }
        fprintf(out, "\n");
        return KC_FLOW_OK;
    }
    if (strcmp(argv[0], "cat") == 0) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        return KC_FLOW_OK;
    }
    if (strcmp(argv[0], "mkdir") == 0) {
        int i;
        for (i = 1; i < argc; i++) {
            if (argv[i][0] == '-') continue;
#ifndef _WIN32
            mkdir(argv[i], 0777);
#else
            CreateDirectoryA(argv[i], NULL);
#endif
        }
        return KC_FLOW_OK;
    }
    return 1;
}

/**
 * Execute one resolved shell command.
 * @param command Shell command.
 * @param flow_path Current flow path.
 * @param input Input data.
 * @param input_size Input size.
 * @param flow Flow parameters.
 * @param node Node parameters.
 * @param out_data Output data pointer.
 * @param out_size Output size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_command(
    const char *command,
    const char *flow_path,
    const void *input,
    size_t input_size,
    const kc_flow_store_t *flow,
    const kc_flow_store_t *node,
    char **out_data,
    size_t *out_size,
    int ignore_error
) {
#ifndef _WIN32
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    char dir[KC_FLOW_MAX_PATH];
    pid_t pid;
    int status;
    char chunk[4096];
    size_t total = 0;
    size_t cap = 0;
    char *data = NULL;
    char **argv = NULL;
    int argc = 0;
    int builtin_rc;

    if (!in || !out) {
        if (in) {
            fclose(in);
        }
        if (out) {
            fclose(out);
        }
        return KC_FLOW_ERROR;
    }
    if (input_size > 0 && fwrite(input, 1, input_size, in) != input_size) {
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    fflush(in);
    rewind(in);

    argv = kc_flow_parse_argv(command, &argc);
    builtin_rc = kc_flow_run_builtin(argc, argv, in, out);
    if (builtin_rc != 1) {
        kc_flow_free_argv(argv);
        if (builtin_rc != KC_FLOW_OK) {
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
        goto capture;
    }

    if (kc_flow_dir(dir, sizeof(dir), flow_path) != KC_FLOW_OK) {
        kc_flow_free_argv(argv);
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    pid = fork();
    if (pid < 0) {
        kc_flow_free_argv(argv);
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    if (pid == 0) {
        setenv("KC_FLOW_FILE", flow_path, 1);
        setenv("KC_FLOW_DIR", dir, 1);
        kc_flow_set_env_store("KC_FLOW_FLOW_PARAM_", flow);
        kc_flow_set_env_store("KC_FLOW_NODE_PARAM_", node);
        kc_flow_set_env_store("KC_FLOW_PARAM_", node);
        dup2(fileno(in), STDIN_FILENO);
        dup2(fileno(out), STDOUT_FILENO);
        if (chdir(dir) != 0) {
            _exit(1);
        }
        if (strpbrk(command, "|&;<>()$`\\\"'*?#~[]") == NULL) {
            execvp(argv[0], argv);
        } else {
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        }
        _exit(127);
    }
    kc_flow_free_argv(argv);
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || (WEXITSTATUS(status) != 0 && !ignore_error)) {
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
capture:
    fflush(out);
    rewind(out);
    while (!feof(out)) {
        size_t n = fread(chunk, 1, sizeof(chunk), out);
        if (n > 0) {
            if (total + n + 1 > cap) {
                char *next;
                cap = cap ? cap * 2 : 4096;
                while (cap < total + n + 1) {
                    cap *= 2;
                }
                next = (char *)realloc(data, cap);
                if (!next) {
                    free(data);
                    fclose(in);
                    fclose(out);
                    return KC_FLOW_ERROR;
                }
                data = next;
            }
            memcpy(data + total, chunk, n);
            total += n;
        }
        if (ferror(out)) {
            free(data);
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
    }
    fclose(in);
    fclose(out);
    if (!data) {
        data = kc_flow_dup("");
        if (!data) {
            return KC_FLOW_ERROR;
        }
    }
    data[total] = '\0';
    *out_data = data;
    *out_size = total;
    return KC_FLOW_OK;
#else
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    char chunk[4096];
    size_t total = 0;
    size_t cap = 0;
    char *data = NULL;
    char **argv = NULL;
    int argc = 0;
    int builtin_rc;
    (void)flow_path;
    (void)flow;
    (void)node;

    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return KC_FLOW_ERROR;
    }
    if (input_size > 0 && fwrite(input, 1, input_size, in) != input_size) {
        fclose(in);
        fclose(out);
        return KC_FLOW_ERROR;
    }
    fflush(in);
    rewind(in);

    argv = kc_flow_parse_argv(command, &argc);
    builtin_rc = kc_flow_run_builtin(argc, argv, in, out);
    if (builtin_rc != 1) {
        kc_flow_free_argv(argv);
        if (builtin_rc != KC_FLOW_OK) {
            fclose(in);
            fclose(out);
            return KC_FLOW_ERROR;
        }
        goto capture;
    }
    kc_flow_free_argv(argv);
    fclose(in);

    {
        FILE *pipe = _popen(command, "r");
        if (!pipe) {
            fclose(out);
            return KC_FLOW_ERROR;
        }
        while (!feof(pipe)) {
            size_t n = fread(chunk, 1, sizeof(chunk), pipe);
            if (n > 0) {
                fwrite(chunk, 1, n, out);
            }
        }
        {
            int st = _pclose(pipe);
            if (st != 0 && !ignore_error) {
                fclose(out);
                return KC_FLOW_ERROR;
            }
        }
    }
capture:
    fflush(out);
    rewind(out);
    while (!feof(out)) {
        size_t n = fread(chunk, 1, sizeof(chunk), out);
        if (n > 0) {
            if (total + n + 1 > cap) {
                char *next;
                cap = cap ? cap * 2 : 4096;
                while (cap < total + n + 1) cap *= 2;
                next = (char *)realloc(data, cap);
                if (!next) {
                    free(data);
                    fclose(out);
                    return KC_FLOW_ERROR;
                }
                data = next;
            }
            memcpy(data + total, chunk, n);
            total += n;
        }
    }
    fclose(out);
    if (!data) {
        data = kc_flow_dup("");
        if (!data) return KC_FLOW_ERROR;
    }
    data[total] = '\0';
    *out_data = data;
    *out_size = total;
    return KC_FLOW_OK;
#endif
}

static int kc_flow_run_node(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow_params,
    const kc_flow_node_t *node,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
);

/**
 * Execute one child flow expansion.
 * @param ctx Context pointer.
 * @param path Child path.
 * @param overrides Parent node parameters.
 * @param input Input branch.
 * @param outputs Output branches.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_child(
    kc_flow_t *ctx,
    const char *path,
    const kc_flow_store_t *overrides,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
) {
    kc_flow_store_t records;
    kc_flow_model_t *model;
    kc_flow_store_t params;
    size_t i;
    int rc = KC_FLOW_OK;

    if (kc_flow_read_records(path, &records) != KC_FLOW_OK) {
        return kc_flow_fail(ctx, "unable to read child flow");
    }
    model = (kc_flow_model_t *)malloc(sizeof(*model));
    if (!model) {
        kc_flow_store_free(&records);
        return kc_flow_fail(ctx, "out of memory");
    }
    if (kc_flow_parse_model(&records, model) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        free(model);
        return kc_flow_fail(ctx, "unable to parse child flow");
    }
    kc_flow_store_free(&records);
    if (kc_flow_validate_model(model, NULL) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "invalid child flow");
    }
    if (kc_flow_store_copy(&params, &model->params) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "unable to copy child parameters");
    }
    for (i = 0; i < overrides->count; ++i) {
        if (kc_flow_store_set(&params, overrides->items[i].key, overrides->items[i].value) != KC_FLOW_OK) {
            rc = kc_flow_fail(ctx, "unable to merge child parameters");
            break;
        }
    }
    for (i = 0; rc == KC_FLOW_OK && i < model->entries.count; ++i) {
        kc_flow_node_t *entry = kc_flow_model_find(model, model->entries.items[i]);
        if (!entry || kc_flow_run_node(ctx, path, model, &params, entry, input, outputs, depth + 1) != KC_FLOW_OK) {
            rc = kc_flow_fail(ctx, "child flow execution failed");
        }
    }
    kc_flow_store_free(&params);
    kc_flow_model_free(model);
    free(model);
    return rc;
}

/**
 * Execute one node.
 * @param ctx Context pointer.
 * @param flow_path Current flow path.
 * @param model Model pointer.
 * @param flow_params Effective flow parameters.
 * @param node Node pointer.
 * @param input Input branch.
 * @param outputs Output branches.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_run_node(
    kc_flow_t *ctx,
    const char *flow_path,
    kc_flow_model_t *model,
    const kc_flow_store_t *flow_params,
    const kc_flow_node_t *node,
    const kc_flow_branch_t *input,
    kc_flow_branches_t *outputs,
    int depth
) {
    kc_flow_store_t node_params;
    kc_flow_branches_t active;
    size_t i;
    int rc = KC_FLOW_OK;

    if (depth > 64) {
        return kc_flow_fail(ctx, "maximum flow depth exceeded");
    }

    if (kc_flow_collect_node(flow_params, node, &node_params) != KC_FLOW_OK) {
        return kc_flow_fail(ctx, "unable to resolve node parameters");
    }
    kc_flow_branches_init(&active);
    if (kc_flow_branches_copy(&active, input->data, input->size) != KC_FLOW_OK) {
        kc_flow_store_free(&node_params);
        return kc_flow_fail(ctx, "unable to create branch");
    }
    if (node->file && *node->file) {
        kc_flow_branches_t next;
        char *file = kc_flow_template(node->file, flow_params, &node_params);
        char child_path[KC_FLOW_MAX_PATH];
        kc_flow_branches_init(&next);
        if (!file || kc_flow_relative_path(child_path, sizeof(child_path), flow_path, file) != KC_FLOW_OK) {
            free(file);
            kc_flow_branches_free(&active);
            kc_flow_store_free(&node_params);
            return kc_flow_fail(ctx, "invalid child flow path");
        }
        free(file);
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            rc = kc_flow_run_child(ctx, child_path, &node_params, &active.items[i], &next, depth + 1);
        }
        kc_flow_branches_free(&active);
        active = next;
    }
    if (rc == KC_FLOW_OK && node->exec && *node->exec) {
        kc_flow_branches_t next;
        kc_flow_branches_init(&next);
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            char *command = kc_flow_template(node->exec, flow_params, &node_params);
            char *out = NULL;
            size_t out_size = 0;
            const char *ignore = kc_flow_store_get(&node->meta, "ignore_error");
            int ignore_val = (ignore && strcmp(ignore, "1") == 0);
            if (!command || kc_flow_run_command(command, flow_path, active.items[i].data, active.items[i].size, flow_params, &node_params, &out, &out_size, ignore_val) != KC_FLOW_OK) {
                free(command);
                rc = kc_flow_fail(ctx, "node command failed");
                break;
            }
            free(command);
            if (kc_flow_branches_take(&next, out, out_size) != KC_FLOW_OK) {
                rc = kc_flow_fail(ctx, "too many branches");
            }
        }
        kc_flow_branches_free(&active);
        active = next;
    }
    if (rc == KC_FLOW_OK && node->links.count > 0) {
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            size_t j;
            for (j = 0; rc == KC_FLOW_OK && j < node->links.count; ++j) {
                kc_flow_node_t *next = kc_flow_model_find(model, node->links.items[j]);
                if (!next) {
                    rc = kc_flow_fail(ctx, "unknown node link");
                } else {
                    rc = kc_flow_run_node(ctx, flow_path, model, flow_params, next, &active.items[i], outputs, depth + 1);
                }
            }
        }
    } else if (rc == KC_FLOW_OK) {
        for (i = 0; rc == KC_FLOW_OK && i < active.count; ++i) {
            rc = kc_flow_branches_copy(outputs, active.items[i].data, active.items[i].size);
        }
    }
    kc_flow_branches_free(&active);
    kc_flow_store_free(&node_params);
    return rc;
}

/**
 * Load one model from a file and context overlays.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Optional explicit entry.
 * @param model Output model.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_load(kc_flow_t *ctx, const char *path, const char *entry, kc_flow_model_t *model) {
    kc_flow_store_t records;
    struct stat st;

    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return kc_flow_fail(ctx, "flow file not found");
    }
    if (kc_flow_read_records(path, &records) != KC_FLOW_OK) {
        return kc_flow_fail(ctx, "unable to read flow file");
    }
    if (kc_flow_apply_overlays(ctx, &records) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        return KC_FLOW_ERROR;
    }
    if (kc_flow_parse_model(&records, model) != KC_FLOW_OK) {
        kc_flow_store_free(&records);
        return kc_flow_fail(ctx, "unable to parse flow file");
    }
    kc_flow_store_free(&records);
    if (kc_flow_validate_model(model, entry) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        return kc_flow_fail(ctx, "invalid flow structure");
    }
    return KC_FLOW_OK;
}

/**
 * Execute one loaded model.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Optional explicit entry.
 * @param input Input buffer.
 * @param input_size Input size.
 * @param output Output pointer.
 * @param output_size Output size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_exec_loaded(
    kc_flow_t *ctx,
    const char *path,
    const char *entry,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    kc_flow_model_t *model;
    kc_flow_store_t params;
    kc_flow_branches_t outputs;
    kc_flow_branch_t start;
    size_t i;
    size_t total = 0;
    char *data;
    char *cursor;
    int rc = KC_FLOW_OK;

    if (!ctx || !path || !output || !output_size || (input_size > 0 && !input)) {
        return kc_flow_fail(ctx, "invalid argument");
    }
    *output = NULL;
    *output_size = 0;
    model = (kc_flow_model_t *)malloc(sizeof(*model));
    if (!model) {
        return kc_flow_fail(ctx, "out of memory");
    }
    if (kc_flow_load(ctx, path, entry, model) != KC_FLOW_OK) {
        free(model);
        return KC_FLOW_ERROR;
    }
    if (kc_flow_store_copy(&params, &model->params) != KC_FLOW_OK) {
        kc_flow_model_free(model);
        free(model);
        return kc_flow_fail(ctx, "unable to copy flow parameters");
    }
    kc_flow_branches_init(&outputs);
    start.data = (char *)(input ? input : "");
    start.size = input_size;
    if (entry) {
        kc_flow_node_t *node = kc_flow_model_find(model, entry);
        rc = node ? kc_flow_run_node(ctx, path, model, &params, node, &start, &outputs, 0) : kc_flow_fail(ctx, "unknown entry");
    } else {
        for (i = 0; rc == KC_FLOW_OK && i < model->entries.count; ++i) {
            kc_flow_node_t *node = kc_flow_model_find(model, model->entries.items[i]);
            rc = node ? kc_flow_run_node(ctx, path, model, &params, node, &start, &outputs, 0) : kc_flow_fail(ctx, "unknown entry");
        }
    }
    for (i = 0; rc == KC_FLOW_OK && i < outputs.count; ++i) {
        total += outputs.items[i].size;
    }
    data = rc == KC_FLOW_OK ? (char *)malloc(total + 1) : NULL;
    if (rc == KC_FLOW_OK && !data) {
        rc = kc_flow_fail(ctx, "out of memory");
    }
    if (rc == KC_FLOW_OK) {
        cursor = data;
        for (i = 0; i < outputs.count; ++i) {
            memcpy(cursor, outputs.items[i].data, outputs.items[i].size);
            cursor += outputs.items[i].size;
        }
        data[total] = '\0';
        *output = data;
        *output_size = total;
    }
    kc_flow_branches_free(&outputs);
    kc_flow_store_free(&params);
    kc_flow_model_free(model);
    free(model);
    return rc;
}

/**
 * Allocate one flow runtime context.
 * @param none Unused.
 * @return Context pointer or NULL on failure.
 */
kc_flow_t *kc_flow_open(void) {
    kc_flow_t *ctx = (kc_flow_t *)calloc(1, sizeof(kc_flow_t));

    if (ctx) {
        ctx->workers = 1;
    }
    return ctx;
}

/**
 * Release one flow runtime context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_flow_close(kc_flow_t *ctx) {
    size_t i;

    if (!ctx) {
        return;
    }
    for (i = 0; i < ctx->overlay_count; ++i) {
        free(ctx->overlays[i].key);
        free(ctx->overlays[i].value);
    }
    free(ctx);
}

/**
 * Append one ordered key-value overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @param value Overlay value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set(kc_flow_t *ctx, const char *key, const char *value) {
    kc_flow_overlay_t *overlay;

    if (!ctx || !key || !value || ctx->overlay_count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return kc_flow_fail(ctx, "invalid set overlay");
    }
    overlay = &ctx->overlays[ctx->overlay_count];
    overlay->kind = KC_FLOW_OVERLAY_SET;
    overlay->key = kc_flow_dup(key);
    overlay->value = kc_flow_dup(value);
    if (!overlay->key || !overlay->value) {
        free(overlay->key);
        free(overlay->value);
        return kc_flow_fail(ctx, "out of memory");
    }
    ctx->overlay_count++;
    return KC_FLOW_OK;
}

/**
 * Append one ordered key removal overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_unset(kc_flow_t *ctx, const char *key) {
    kc_flow_overlay_t *overlay;

    if (!ctx || !key || ctx->overlay_count >= KC_FLOW_MAX_RECORDS || !kc_flow_key_valid(key)) {
        return kc_flow_fail(ctx, "invalid unset overlay");
    }
    overlay = &ctx->overlays[ctx->overlay_count];
    overlay->kind = KC_FLOW_OVERLAY_UNSET;
    overlay->key = kc_flow_dup(key);
    if (!overlay->key) {
        return kc_flow_fail(ctx, "out of memory");
    }
    ctx->overlay_count++;
    return KC_FLOW_OK;
}

/**
 * Set the runtime worker count hint.
 * @param ctx Context pointer.
 * @param workers Worker count greater than zero.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set_workers(kc_flow_t *ctx, size_t workers) {
    if (!ctx || workers == 0) {
        return kc_flow_fail(ctx, "invalid worker count");
    }
    ctx->workers = workers;
    return KC_FLOW_OK;
}

/**
 * Execute one flow file from its declared entries.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param output Owned output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec(
    kc_flow_t *ctx,
    const char *path,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    return kc_flow_exec_loaded(ctx, path, NULL, input, input_size, output, output_size);
}

/**
 * Execute one flow file from one explicit entry node.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Entry node reference.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param output Owned output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec_entry(
    kc_flow_t *ctx,
    const char *path,
    const char *entry,
    const void *input,
    size_t input_size,
    char **output,
    size_t *output_size
) {
    if (!entry || !*entry) {
        return kc_flow_fail(ctx, "invalid entry");
    }
    return kc_flow_exec_loaded(ctx, path, entry, input, input_size, output, output_size);
}

/**
 * Release one output buffer produced by the runtime.
 * @param output Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *output) {
    free(output);
}
