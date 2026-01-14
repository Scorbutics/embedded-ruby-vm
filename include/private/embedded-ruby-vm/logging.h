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
 * Custom output callback type
 * @param line Complete log line (null-terminated, without newline)
 * @param stream Which stream this came from (stdout or stderr)
 * @param context User-defined context pointer
 */
typedef int (*logging_custom_output_func_t)(const char* line, log_stream_t stream, void* context);

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

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
