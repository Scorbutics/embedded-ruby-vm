#ifndef LOG_LISTENER_PUBLIC_H
#define LOG_LISTENER_PUBLIC_H

#include <stddef.h>
#include <sys/types.h>  /* ssize_t for logging_emit_to_terminal */

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
 * Log severity level for a single log line.
 *
 * For LOG_STREAM_VMLOGGER lines, the level reflects which VMLogger method
 * produced the line (VMLogger.debug -> DEBUG, .info -> INFO, .error -> ERROR);
 * the parsing happens inside the logging system from an in-band tag emitted by
 * safe_runner.rb, so consumers see the level directly.
 *
 * For non-VMLOGGER streams, the level is derived from the stream identity:
 * RUBY_STDERR and NATIVE_STDERR map to ERROR; the remaining stdout streams
 * map to INFO. This keeps the field meaningful for every line without
 * forcing consumers to special-case the source.
 */
typedef enum {
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/**
 * Log listener callback structure.
 *
 * Passed through the C core and across the JNI/cinterop boundary.
 * Every log line — stdout, stderr, VMLogger, native — is delivered through
 * the single on_log_message callback, with source and severity attached.
 */
struct LogListener;

typedef void (*LogMessageFunc)(struct LogListener* listener,
                                const char* message,
                                log_stream_t source,
                                log_level_t level);

typedef struct LogListener {
    void* context;
    void* user_data;
    LogMessageFunc on_log_message;
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
    listener->on_log_message = NULL;
}

/**
 * Get the original stdout file descriptor (before logging redirected it).
 *
 * The logging system redirects process-wide stdout/stderr (fd 1 / fd 2) to
 * pipes so the logger thread can capture native C library output (printf,
 * std::cerr, SFML errors, etc.) and forward it to registered listeners.
 *
 * CALLBACK CONTRACT — log listener callbacks SHOULD NOT write to fd 1 / fd 2.
 * Doing so feeds the bytes back into the pipes the logger is reading,
 * triggering another callback invocation. The logging system has
 * safety nets — fd 1 / fd 2 are O_NONBLOCK so a feedback write can't
 * deadlock, and the dispatcher emits a one-shot "possible callback
 * feedback loop" warning if the same line repeats fast — but the
 * resulting CPU spin still wastes work. For callback output prefer:
 *
 *   1. logging_emit_to_terminal() / _err() (below) — direct write to the
 *      original terminal FD, bypassing the pipes entirely.
 *   2. The platform's async log channel:
 *      - Android (C):    __android_log_print / __android_log_write
 *      - Android (JVM):  android.util.Log.d / .i / .w / .e
 *      - Apple:          os_log
 *      - Linux/systemd:  sd_journal_print
 *
 * If you need the raw FD (e.g. to pass to a logging library), this
 * function returns it. Most callers want logging_emit_to_terminal() —
 * see below.
 *
 * @return Original stdout FD (>= 0), or -1 if logging has not redirected stdout
 */
int logging_get_original_stdout_fd(void);

/**
 * Get the original stderr file descriptor (before logging redirected it).
 *
 * Same callback contract and usage notes as logging_get_original_stdout_fd.
 *
 * @return Original stderr FD (>= 0), or -1 if logging has not redirected stderr
 * @see logging_get_original_stdout_fd
 */
int logging_get_original_stderr_fd(void);

/**
 * Safe terminal-emit helpers for use FROM INSIDE a log listener callback.
 *
 * Writes the given byte range to the pre-redirect terminal stdout / stderr,
 * bypassing the logging pipes entirely. This is the right API for a
 * callback that wants to surface output to the operator without feeding
 * the bytes back into the logging pipeline (which would re-dispatch the
 * same line and burn CPU in a spin loop).
 *
 * The bytes are written verbatim — no newline is appended. Partial writes
 * and EINTR / EAGAIN are handled internally; the function only returns
 * early on a hard write error.
 *
 * Safe to call from any thread once logging is initialized. Returns -1 if
 * called before logging has redirected stdout/stderr (the saved original
 * FDs aren't available yet), or on a write error.
 *
 * Typical usage:
 *
 *   static int on_log_line(const char* line, log_stream_t stream,
 *                          log_level_t level, void* ctx) {
 *       char buf[256];
 *       int n = snprintf(buf, sizeof(buf), "[app] %s\n", line);
 *       if (n > 0) logging_emit_to_terminal(buf, (size_t)n);
 *       return 0;
 *   }
 */
ssize_t logging_emit_to_terminal(const char* buf, size_t len);

/** Same as logging_emit_to_terminal, but targets the original stderr. */
ssize_t logging_emit_to_terminal_err(const char* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LOG_LISTENER_PUBLIC_H */
