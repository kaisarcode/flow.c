/**
 * flow.h - Branch-oriented flow runtime API.
 * Summary: Public API for loading and executing flat flow documents.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_FLOW_H
#define KC_FLOW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_flow kc_flow_t;

#define KC_FLOW_OK 0
#define KC_FLOW_ERROR -1

/**
 * Allocate one flow runtime context.
 * @param none Unused.
 * @return Context pointer or NULL on failure.
 */
kc_flow_t *kc_flow_open(void);

/**
 * Release one flow runtime context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_flow_close(kc_flow_t *ctx);

/**
 * Append one ordered key-value overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @param value Overlay value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set(kc_flow_t *ctx, const char *key, const char *value);

/**
 * Append one ordered key removal overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_unset(kc_flow_t *ctx, const char *key);

/**
 * Set the runtime worker count hint.
 * @param ctx Context pointer.
 * @param workers Worker count greater than zero.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set_workers(kc_flow_t *ctx, size_t workers);

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
);

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
);

/**
 * Release one output buffer produced by the runtime.
 * @param output Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *output);

/**
 * Returns the last error message from a flow context.
 * @param ctx Context pointer.
 * @return Static error string.
 */
const char *kc_flow_strerror(kc_flow_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
