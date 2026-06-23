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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_flow kc_flow_t;

#define KC_FLOW_OK 0
#define KC_FLOW_ERROR -1

/**
 * Options struct for flow configuration.
 */
typedef struct kc_flow_options {
    int _unused;
} kc_flow_options_t;

/**
 * Callback type for library-level signal handling.
 * @param ctx Context pointer.
 */
typedef void (*kc_flow_signal_callback_t)(kc_flow_t *ctx);

/**
 * Allocate one flow runtime context.
 * @param out Pointer to receive context pointer.
 * @param opts Configuration options.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_open(kc_flow_t **out, const kc_flow_options_t *opts);

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_flow_options_t kc_flow_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_flow_options_load_env(kc_flow_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_flow_options_free(kc_flow_options_t *opts);

/**
 * Register a handler for a library-level signal number.
 * @param ctx Context pointer.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_on_signal(kc_flow_t *ctx, int sig, kc_flow_signal_callback_t cb);

/**
 * Raise a library-level signal.
 * @param ctx Context pointer.
 * @param sig Signal number to raise.
 * @return KC_FLOW_OK if handled, or KC_FLOW_ERROR if no handler.
 */
int kc_flow_raise_signal(kc_flow_t *ctx, int sig);

/**
 * Set the internal signal-listener context.
 * @param ctx Context pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR if ctx is NULL.
 */
int kc_flow_listen_signals(kc_flow_t *ctx);

/**
 * Wire an OS signal to the library signal listener.
 * @param ctx Context pointer.
 * @param sig_id OS signal number.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_listen_signal(kc_flow_t *ctx, int sig_id);

/**
 * Generic signal-listener compatible with signal() / sigaction().
 * @param sig OS signal number.
 * @return None.
 */
void kc_flow_signal_listener(int sig);

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
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_flow_version(void);

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
