#ifndef ASSETS_LOGGING_H
#define ASSETS_LOGGING_H

#include <stdio.h>

/**
 * Assets logging system
 *
 * Provides three levels of logging:
 * - DEBUG: Verbose debug information (disabled in release builds)
 * - INFO: Informational messages (always enabled)
 * - ERROR: Error messages (always enabled)
 *
 * On Android with JNI logging enabled, logs go to logcat via __android_log_print.
 * On other platforms, logs go to stderr/stdout.
 */

// Use Android logging if available (when building with JNI Android logging support)
#ifdef __ANDROID__
    #include <android/log.h>
    #define ASSETS_LOG_TAG "RubyVM-Assets"

    // Debug & Info logging - can be disabled by defining NDEBUG
    #ifdef NDEBUG
        #define ASSETS_DEBUG_LOG(fmt, ...) ((void)0)
        #define ASSETS_INFO_LOG(fmt, ...) ((void)0)
    #else
        #define ASSETS_DEBUG_LOG(fmt, ...) \
            __android_log_print(ANDROID_LOG_DEBUG, ASSETS_LOG_TAG, fmt, ##__VA_ARGS__)

        #define ASSETS_INFO_LOG(fmt, ...) \
            __android_log_print(ANDROID_LOG_INFO, ASSETS_LOG_TAG, fmt, ##__VA_ARGS__)
    #endif

    // Error logging - always enabled
    #define ASSETS_ERROR_LOG(fmt, ...) \
        __android_log_print(ANDROID_LOG_ERROR, ASSETS_LOG_TAG, fmt, ##__VA_ARGS__)

#else
    // Non-Android platforms: use stderr/stdout

    // Debug & Info logging - can be disabled by defining NDEBUG
    #ifdef NDEBUG
        #define ASSETS_DEBUG_LOG(fmt, ...) ((void)0)
        #define ASSETS_INFO_LOG(fmt, ...) ((void)0)
    #else
        #define ASSETS_DEBUG_LOG(fmt, ...) do { \
            fprintf(stderr, "[ASSETS_DEBUG] " fmt "\n", ##__VA_ARGS__); \
            fflush(stderr); \
        } while(0)

        #define ASSETS_INFO_LOG(fmt, ...) do { \
            fprintf(stdout, "[ASSETS] " fmt "\n", ##__VA_ARGS__); \
            fflush(stdout); \
        } while(0)
    #endif

    // Error logging - always enabled, goes to stderr
    #define ASSETS_ERROR_LOG(fmt, ...) do { \
        fprintf(stderr, "[ASSETS_ERROR] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)
#endif

#endif // ASSETS_LOGGING_H
