#ifndef LOGGING_H
#define LOGGING_H

#include "embedded-ruby-vm/log-listener.h"  /* log_stream_t, LogListener */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Detailed logging error codes
 * Organized by category for easier diagnosis
 */
typedef enum {
    /* Success */
    LOGGING_ERROR_NONE = 0,

    /* Initialization errors (-1 to -9) */
    LOGGING_ERROR_NOT_INITIALIZED = -1,
    LOGGING_ERROR_ALREADY_INITIALIZED = -2,
    LOGGING_ERROR_INVALID_PARAMETER = -3,
    LOGGING_ERROR_MEMORY_ALLOCATION = -4,

    /* Stream redirection errors (-10 to -19) */
    LOGGING_ERROR_SOCKETPAIR_FAILED = -10,
    LOGGING_ERROR_DUP2_FAILED = -11,
    LOGGING_ERROR_STDOUT_REDIRECT_FAILED = -12,
    LOGGING_ERROR_STDERR_REDIRECT_FAILED = -13,

    /* Thread errors (-20 to -29) */
    LOGGING_ERROR_THREAD_CREATE_FAILED = -20,
    LOGGING_ERROR_THREAD_JOIN_FAILED = -21,
    LOGGING_ERROR_THREAD_ALREADY_RUNNING = -22,

    /* I/O errors (-30 to -39) */
    LOGGING_ERROR_READ_FAILED = -30,
    LOGGING_ERROR_WRITE_FAILED = -31,
    LOGGING_ERROR_SELECT_FAILED = -32,

    /* Callback errors (-40 to -49) */
    LOGGING_ERROR_NATIVE_CALLBACK_FAILED = -40,
    LOGGING_ERROR_CUSTOM_CALLBACK_FAILED = -41,
    LOGGING_ERROR_CALLBACK_NOT_FOUND = -42,
    LOGGING_ERROR_CALLBACK_ALREADY_EXISTS = -43,

    /* State errors (-50 to -59) */
    LOGGING_ERROR_NOT_RUNNING = -50,
    LOGGING_ERROR_MUTEX_LOCK_FAILED = -51
} logging_error_t;

/**
 * Native logging function type (e.g., for Android logcat)
 * @param priority Log priority level
 * @param tag Log tag
 * @param text Log message
 */
typedef int (*logging_native_logging_func_t)(int priority, const char* tag, const char* text);

/**
 * Custom output callback type.
 *
 * @param line    Complete log line (null-terminated, without newline)
 * @param stream  Which stream this came from (one of LOG_STREAM_RUBY_STDOUT,
 *                LOG_STREAM_RUBY_STDERR, LOG_STREAM_VMLOGGER,
 *                LOG_STREAM_NATIVE_STDOUT, LOG_STREAM_NATIVE_STDERR)
 * @param context User-defined context pointer
 *
 * EXECUTION CONTEXT
 * -----------------
 * Callbacks run on the logging system's dedicated dispatch worker thread —
 * NOT the thread that runs the script and NOT the thread that drains the
 * pipes. Callback latency therefore does not stall pipe drainage.
 *
 * CONTRACT — DO NOT WRITE TO fd 1 / fd 2 FROM A CALLBACK
 * -------------------------------------------------------
 * Callbacks MUST NOT write to the redirected stdout/stderr file descriptors
 * (fd 1, fd 2). Bytes written there go into the same pipes the logger
 * thread is reading, which would cause every callback's output to trigger
 * another callback invocation — bounded only by the dispatch queue cap
 * (DISPATCH_QUEUE_MAX_LOG_LINES; lines beyond it are dropped, with periodic
 * warnings surfaced via logcat).
 *
 * Use the platform's async log channel instead — it bypasses the pipes:
 *   - Android (C):    __android_log_print / __android_log_write
 *   - Android (JVM):  android.util.Log.d / .i / .w / .e
 *   - Apple:          os_log
 *   - Linux/systemd:  sd_journal_print
 *
 * If a callback genuinely needs a "raw terminal" channel for diagnostics,
 * write() directly to logging_get_original_stdout_fd() /
 * logging_get_original_stderr_fd() — those are the pre-redirect FDs and do
 * not feed back into the pipes.
 *
 * THREAD SAFETY
 * -------------
 * The dispatch worker holds an internal lock for the duration of the
 * callback iteration (the same lock taken by logging_swap_listener,
 * logging_add_custom_output, logging_remove_custom_output). Do NOT call
 * those functions from inside the callback — that would deadlock.
 *
 * @return 0 on success; non-zero is logged but does not stop dispatch.
 */
typedef int (*logging_custom_output_func_t)(const char* line, log_stream_t stream, log_level_t level, void* context);

/**
 * Initialize the logging system with an application name
 * @param appname Application name for log tag
 * @return 0 on success, negative on error
 */
int logging_init(const char* appname);

/**
 * Shutdown the logging system and free resources
 * @return 0 on success, negative on error
 */
int logging_shutdown(void);

/**
 * Add a native logging function
 * @param func Native logging function to add
 * @return 0 on success, negative on error
 */
int logging_add_native_function(logging_native_logging_func_t func);

/**
 * Remove a native logging function
 * @param func Native logging function to remove
 * @return 0 on success, -1 if not found
 */
int logging_remove_native_function(logging_native_logging_func_t func);

/**
 * Add a custom output callback
 * Thread and redirection start automatically when the first callback is added
 * @param func Callback function
 * @param context User-defined context (can be NULL)
 * @return 0 on success, negative on error
 */
int logging_add_custom_output(logging_custom_output_func_t func, void* context);

/**
 * Remove a custom output callback
 * Thread and redirection stop automatically when the last callback is removed
 * @param func Callback function to remove
 * @param context Context pointer to match (must match both func and context)
 * @return 0 on success, -1 if not found
 */
int logging_remove_custom_output(logging_custom_output_func_t func, void* context);

/* ---- Per-interpreter listener registry --------------------------------
 *
 * Each RubyInterpreter registers itself here at create time with its id +
 * LogListener. The dispatch path parses the line for an in-band
 * "[<id>]" prefix the producing worker Thread emitted via the Ruby-side
 * TaggedIO wrapper (see fifo_interpreter.rb), looks up the matching
 * listener, strips the prefix, and invokes it. Lines without a prefix
 * (native printf, std::cerr, anything outside a tagged worker) are
 * delivered to whichever listener registered first — typically the
 * long-running interpreter, which is the natural sink for native output.
 *
 * Multiple registrations for the same id are accepted but the latest
 * wins; in practice only the first execute_sync after create touches
 * this, and re-registration with the same context is a no-op.
 *
 * Replaces the older single-slot vm->log_listener / logging_swap_listener
 * pair: that design only supported one active listener at a time and
 * required serialized rebinding on every execute_sync, which broke
 * overlapping interpreter lifetimes (an ephemeral compile interpreter
 * could leave the persistent loader interpreter's listener unbound on
 * destroy).
 */

/**
 * Register a listener for the given Ruby interpreter id. Pass an id of
 * [LOG_NATIVE_INTERPRETER_ID] is NOT supported here — that id is reserved
 * for the dispatch's "no tag found" fallback path and matches all
 * registered listeners by ordinal (oldest wins).
 *
 * @param interpreter_id The id assigned to the interpreter (>0).
 * @param listener       Copied by value; caller may discard their copy.
 * @return 0 on success, -1 on bad input.
 */
int logging_register_interpreter_listener(int interpreter_id, LogListener listener);

/**
 * Unregister the listener bound to [interpreter_id]. Drops it from the
 * registry under the logging-system lock so any in-flight dispatch
 * completes before the entry goes away. Safe to call multiple times.
 *
 * @return 0 on success (entry removed or never present), -1 on bad input.
 */
int logging_unregister_interpreter_listener(int interpreter_id);

/**
 * Platform-specific logging setup (e.g., Android logcat integration)
 *
 * This function is called automatically by logging_init() to register
 * platform-specific native logging functions.
 *
 * Platform-specific implementations should call logging_add_native_function()
 * to register their native logger (e.g., Android's __android_log_write).
 *
 * Default implementation (weak symbol) does nothing.
 * Platform-specific modules can provide a strong symbol to override.
 *
 * @return 0 on success, negative on error
 */
void logging_setup_platform_native(void);

/**
 * Get the last error that occurred in the current thread
 * Thread-safe: Each thread has its own error state
 *
 * @return Error code from logging_error_t enum
 */
logging_error_t logging_get_last_error(void);

/**
 * Get a human-readable description of an error code
 *
 * @param error Error code from logging_error_t enum
 * @return String description of the error (never NULL)
 */
const char* logging_error_string(logging_error_t error);

/**
 * Clear the last error for the current thread
 * Resets the error state to LOGGING_ERROR_NONE
 */
void logging_clear_last_error(void);

/**
 * Get a file descriptor for a specific log stream
 * This allows external code to write directly to a specific log stream
 *
 * Use case: Ruby VM can get FDs for RUBY_STDOUT, RUBY_STDERR, and VMLOGGER
 * to redirect different output sources to different streams
 *
 * @param stream The log stream type to get FD for
 * @return File descriptor (>= 0) on success, negative on error
 *
 * Note: Only returns FDs for non-native streams (RUBY_STDOUT, RUBY_STDERR, VMLOGGER)
 * Returns LOGGING_ERROR_INVALID_PARAMETER for NATIVE_STDOUT/NATIVE_STDERR
 */
int logging_get_stream_fd(log_stream_t stream);

/**
 * Get the original stdout file descriptor (before logging redirected it).
 *
 * Use this to write output that should NOT feed back into the logging system.
 * For example, log listener callbacks should write to this FD instead of
 * using printf/puts which would go to the redirected stdout and create a
 * feedback loop.
 *
 * @return Original stdout FD (>= 0), or -1 if logging has not redirected stdout
 */
int logging_get_original_stdout_fd(void);

/**
 * Get the original stderr file descriptor (before logging redirected it).
 *
 * @return Original stderr FD (>= 0), or -1 if logging has not redirected stderr
 * @see logging_get_original_stdout_fd
 */
int logging_get_original_stderr_fd(void);

/**
 * Atomically replace a LogListener under the logging system's internal lock.
 *
 * The dispatch worker holds this same lock for the entire duration of every
 * callback invocation (see dispatch_invoke_custom_callbacks in logging.c).
 * By taking it here, we guarantee that after this function returns, no
 * callback will ever be dispatched through the OLD listener — any in-flight
 * callback that was reading the old listener has already completed.
 *
 * Use this whenever the listener bound to a RubyVM (or any structure read
 * by callbacks) is being replaced or torn down. Without this protection,
 * the dispatch worker can race with the swap and dispatch through a freed
 * context, producing a use-after-free crash.
 *
 * @param dest Destination LogListener to overwrite (must remain valid for
 *             the entire lifetime of any callback that reads it).
 * @param src  New LogListener value to install (copied by value).
 */
void logging_swap_listener(LogListener* dest, LogListener src);

/**
 * Reset the script completion sentinel state.
 * Must be called before each script execution to prepare for a fresh wait.
 */
void logging_reset_sentinel(void);

/**
 * Wait for the script completion sentinel to be detected in the log stream.
 *
 * The sentinel is sent by fifo_interpreter.rb via VMLogger after a script
 * finishes and all output has been flushed. The logging thread intercepts it
 * on LOG_STREAM_VMLOGGER (processed after RUBY_STDOUT and RUBY_STDERR),
 * guaranteeing all user-visible output has been dispatched to callbacks.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return 0 if sentinel received, -1 on timeout
 */
int logging_wait_for_sentinel(int timeout_ms);

/**
 * Check if the script completion sentinel has been received (non-blocking).
 * @return 1 if received, 0 if not
 */
int logging_sentinel_received(void);

/**
 * Callback type for drain barriers. Fired by the dispatch worker once every
 * LOG_LINE item buffered before the corresponding logging_drain_async()
 * call has been delivered to all custom-output callbacks. (FIFO ordering
 * is preserved because drain barriers traverse the same dispatch queue as
 * LOG_LINE items.)
 *
 * Execution context:
 *   - Runs on the dispatch worker thread, NOT the logger thread that reads
 *     the pipes. The logger thread continues draining while this callback
 *     runs, so a slow drain callback no longer blocks pipe reads.
 *   - Same NO-stdout/stderr CONTRACT as logging_custom_output_func_t (see
 *     above): do NOT write to fd 1 / fd 2; use __android_log_print / Log.x
 *     / os_log / sd_journal_print, or write() to
 *     logging_get_original_stdout_fd() / _stderr_fd().
 */
typedef void (*logging_drain_cb_t)(void* user_data);

/**
 * Asynchronously install a one-shot drain barrier.
 *
 * Atomically (a) appends the (cb, user_data) pair to the FIFO drain queue
 * and (b) writes a barrier marker to the VMLOGGER pipe. When the logging
 * thread eventually reads that marker, it pops the head of the queue and
 * invokes the callback. Atomicity ensures pipe order matches queue order
 * even when multiple threads call this concurrently.
 *
 * @param cb        Callback to invoke once the barrier has been observed.
 * @param user_data Opaque pointer passed through to cb.
 * @return 0 on success; -1 if logging is not running; -2 on pipe write failure.
 */
int logging_drain_async(logging_drain_cb_t cb, void* user_data);

/**
 * Synchronous drain: blocks the calling thread until the logging system has
 * finished dispatching every callback for data buffered before this call.
 *
 * Built on top of logging_drain_async() — the implementation registers an
 * internal callback that signals a condition variable, then waits on it.
 *
 * @param timeout_ms Maximum wait in milliseconds. Pass 0 to block forever.
 * @return 0 on successful drain; -1 on timeout; -2 if logging isn't running
 *         (which is also a "drain successful" state — nothing to drain).
 */
int logging_drain(int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
