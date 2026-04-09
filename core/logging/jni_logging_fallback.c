/**
 * Weak fallback implementations of jni_log_write and jni_log_printf.
 *
 * When the JNI module is linked, its strong symbols in jni/jni_logging.c
 * override these. When JNI is not linked (pure native builds), these
 * weak symbols provide a simple fprintf(stderr, ...) fallback so that
 * logging.c can call jni_log_printf unconditionally.
 */
#include "embedded-ruby-vm/jni_logging.h"
#include <stdio.h>
#include <stdarg.h>

__attribute__((weak))
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message) {
    (void)priority;
    (void)tag;
    (void)message;
}

__attribute__((weak))
void jni_log_write(JNILogPriority priority, const char* tag, const char* message) {
    jni_log_impl(priority, tag, message);
}

__attribute__((weak))
void jni_log_printf(JNILogPriority priority, const char* tag, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    jni_log_impl(priority, tag, buffer);
}
