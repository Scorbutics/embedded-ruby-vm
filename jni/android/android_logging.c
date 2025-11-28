#include <android/log.h>
#include "../jni_logging.h"

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
