#include "jni_logging.h"
#include <stdio.h>
#include <stdarg.h>

/**
 * Default weak implementation of logging - does nothing.
 * Platform-specific modules can provide a strong symbol to override this.
 */
__attribute__((weak))
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message) {
    // Default no-op implementation
    // This will be overridden by platform-specific modules (e.g., android logging)
    (void)priority;
    (void)tag;
    (void)message;
}

void jni_log_write(JNILogPriority priority, const char* tag, const char* message) {
    jni_log_impl(priority, tag, message);
}

void jni_log_printf(JNILogPriority priority, const char* tag, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    jni_log_impl(priority, tag, buffer);
}
