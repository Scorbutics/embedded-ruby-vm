#ifndef LOG_LISTENER_PUBLIC_H
#define LOG_LISTENER_PUBLIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log stream type - identifies the source of log messages.
 *
 * This enum is part of the public API and is used by both internal
 * logging infrastructure and external consumers via ruby-api-loader.h.
 *
 * LOG_STREAM_RUBY_STDOUT: Ruby VM stdout (puts, print, p)
 * LOG_STREAM_RUBY_STDERR: Ruby VM stderr (warn, raise)
 * LOG_STREAM_VMLOGGER: VMLogger output (VMLogger.debug/info/error)
 * LOG_STREAM_NATIVE_STDOUT: Native C code stdout
 * LOG_STREAM_NATIVE_STDERR: Native C code stderr
 */
typedef enum {
    LOG_STREAM_RUBY_STDOUT = 1,
    LOG_STREAM_RUBY_STDERR = 2,
    LOG_STREAM_VMLOGGER = 3,
    LOG_STREAM_NATIVE_STDOUT = 4,
    LOG_STREAM_NATIVE_STDERR = 5
} log_stream_t;

/**
 * Log listener callback types and structure.
 *
 * The LogListener structure is passed through the C core and across
 * the JNI/cinterop boundary. It contains callback function pointers
 * and an opaque context pointer for the caller's use.
 */
struct LogListener;

typedef void (*LogAcceptFunc)(struct LogListener* listener, const char* lineMessage);
typedef void (*LogErrorFunc)(struct LogListener* listener, const char* errorMessage);
typedef void (*LogMessageFunc)(struct LogListener* listener, const char* message, log_stream_t source);

typedef struct LogListener {
    void* context;
    void* user_data;
    LogAcceptFunc accept;           /* Legacy callback for stdout (deprecated) */
    LogErrorFunc on_log_error;      /* Legacy callback for stderr (deprecated) */
    LogMessageFunc on_log_message;  /* New callback with source information */
} LogListener;

/**
 * Initialize a LogListener to safe defaults (all fields zeroed).
 * Always call this before setting individual callback fields to avoid
 * uninitialized function pointers causing crashes.
 */
static inline void log_listener_init(LogListener* listener) {
    if (!listener) return;
    listener->context = NULL;
    listener->user_data = NULL;
    listener->accept = NULL;
    listener->on_log_error = NULL;
    listener->on_log_message = NULL;
}

/**
 * Get the original stdout file descriptor (before logging redirected it).
 *
 * The logging system redirects process-wide stdout/stderr to capture native
 * C library output. Log listener callbacks MUST use this FD for their own
 * output to avoid a feedback loop (callback writes to stdout -> logging
 * thread reads it -> calls callback again -> infinite loop -> crash).
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

#ifdef __cplusplus
}
#endif

#endif /* LOG_LISTENER_PUBLIC_H */
