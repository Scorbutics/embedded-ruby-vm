#include <android/log.h>
#include "embedded-ruby-vm/jni_logging.h"
#include "embedded-ruby-vm/logging.h"

/**
 * Android-specific implementation of JNI logging.
 * This provides a STRONG symbol that overrides the weak default in jni_logging.c
 *
 * Uses Android's __android_log_write to forward logs to logcat.
 */
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message) {
    // Map JNI log priorities to Android log priorities
    // They use the same values, so we can cast directly
    int android_priority;

    switch (priority) {
        case JNI_LOG_VERBOSE: android_priority = ANDROID_LOG_VERBOSE; break;
        case JNI_LOG_DEBUG:   android_priority = ANDROID_LOG_DEBUG;   break;
        case JNI_LOG_INFO:    android_priority = ANDROID_LOG_INFO;    break;
        case JNI_LOG_WARN:    android_priority = ANDROID_LOG_WARN;    break;
        case JNI_LOG_ERROR:   android_priority = ANDROID_LOG_ERROR;   break;
        case JNI_LOG_FATAL:   android_priority = ANDROID_LOG_FATAL;   break;
        default:              android_priority = ANDROID_LOG_DEFAULT; break;
    }

    __android_log_write(android_priority, tag ? tag : "JNI", message ? message : "");
}

/**
 * Native logging function for the logging system to use Android logcat.
 * This gets registered with logging_add_native_function().
 *
 * @param priority Log priority (matches LOG_* enum in logging.c)
 * @param tag Log tag
 * @param text Log message
 * @return 0 on success
 */
static int android_native_log_function(int priority, const char* tag, const char* text) {
    // Map generic log priorities to Android priorities
    int android_priority;

    // priority values from logging.c enum:
    // LOG_UNKNOWN=0, LOG_DEFAULT=1, LOG_VERBOSE=2, LOG_DEBUG=3,
    // LOG_INFO=4, LOG_WARN=5, LOG_ERROR=6, LOG_FATAL=7, LOG_SILENT=8
    switch (priority) {
        case 2:  android_priority = ANDROID_LOG_VERBOSE; break; // LOG_VERBOSE
        case 3:  android_priority = ANDROID_LOG_DEBUG;   break; // LOG_DEBUG
        case 4:  android_priority = ANDROID_LOG_INFO;    break; // LOG_INFO
        case 5:  android_priority = ANDROID_LOG_WARN;    break; // LOG_WARN
        case 6:  android_priority = ANDROID_LOG_ERROR;   break; // LOG_ERROR
        case 7:  android_priority = ANDROID_LOG_FATAL;   break; // LOG_FATAL
        default: android_priority = ANDROID_LOG_DEFAULT; break;
    }

    return __android_log_write(android_priority, tag ? tag : "RubyVM", text ? text : "") == 1 ? 0 : -1;
}

/**
 * Android-specific platform logging setup.
 * This provides a STRONG symbol that overrides the weak default in logging.c
 *
 * Registers Android's __android_log_write as the native logging function
 * so all Ruby VM logs go to logcat.
 */
void logging_setup_platform_native(void) {
    // Register Android logcat as the native logging backend
    logging_add_native_function(android_native_log_function);
}
