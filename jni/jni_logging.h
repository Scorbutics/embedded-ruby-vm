#ifndef JNI_LOGGING_H
#define JNI_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log levels matching Android's log priorities for compatibility,
 * but platform-agnostic.
 */
typedef enum {
    JNI_LOG_UNKNOWN = 0,
    JNI_LOG_DEFAULT = 1,
    JNI_LOG_VERBOSE = 2,
    JNI_LOG_DEBUG = 3,
    JNI_LOG_INFO = 4,
    JNI_LOG_WARN = 5,
    JNI_LOG_ERROR = 6,
    JNI_LOG_FATAL = 7,
    JNI_LOG_SILENT = 8
} JNILogPriority;

/**
 * Platform-specific logging implementation.
 *
 * This function uses WEAK linkage, allowing platform-specific modules
 * to override it with their own implementation. The default (weak)
 * implementation is a no-op.
 *
 * Platform modules (e.g., jni/android/) provide strong symbol implementations.
 * Users can also provide their own implementation for custom platforms.
 *
 * @param priority Log priority level
 * @param tag Log tag/category (e.g., "RubyVM")
 * @param message Log message
 */
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message)
    __attribute__((weak));

/**
 * Public logging functions used by JNI code.
 * These call jni_log_impl() internally.
 */
void jni_log_write(JNILogPriority priority, const char* tag, const char* message);
void jni_log_printf(JNILogPriority priority, const char* tag, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // JNI_LOGGING_H
