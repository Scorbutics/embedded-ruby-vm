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

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
