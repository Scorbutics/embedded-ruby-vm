#ifndef LOGGING_H
#define LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log stream type
 */
typedef enum {
    LOG_STREAM_STDOUT = 1,
    LOG_STREAM_STDERR = 2
} log_stream_t;

/**
 * Native logging function type (e.g., for Android logcat)
 * @param priority Log priority level
 * @param tag Log tag
 * @param text Log message
 */
typedef int (*logging_native_logging_func_t)(int priority, const char* tag, const char* text);

/**
 * Custom output callback type
 * @param line Complete log line (null-terminated, without newline)
 * @param stream Which stream this came from (stdout or stderr)
 * @param context User-defined context pointer
 */
typedef void (*logging_custom_output_func_t)(const char* line, log_stream_t stream, void* context);

/**
 * Set the native logging function
 */
void LoggingSetNativeLoggingFunction(logging_native_logging_func_t func);

/**
 * Set custom output callback for receiving log lines
 * @param func Callback function
 * @param context User-defined context (can be NULL)
 */
void LoggingSetCustomOutputCallback(logging_custom_output_func_t func, void* context);

/**
 * Start logging thread
 * @param appname Application name for log tag
 * @return 0 on success, negative on error
 */
int LoggingThreadRun(const char* appname);

/**
 * Stop logging thread gracefully
 */
void LoggingThreadStop(void);

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
