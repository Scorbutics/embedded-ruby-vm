/**
 * Debug logging macro - can be disabled by defining NDEBUG
 *
 * On Android, logs go to logcat via __android_log_print.
 * On other platforms, logs go to stderr.
 */

// Use Android logging if available (when building with JNI Android logging support)
#ifdef __ANDROID__
    #include <android/log.h>
    #define DEBUG_LOG_TAG "RubyVM-Debug"

    #ifdef NDEBUG
        #define DEBUG_LOG(fmt, ...) ((void)0)
    #else
        #define DEBUG_LOG(fmt, ...) \
            __android_log_print(ANDROID_LOG_DEBUG, DEBUG_LOG_TAG, fmt, ##__VA_ARGS__)
    #endif

#else
    // Non-Android platforms: use stderr

    #ifdef NDEBUG
        #define DEBUG_LOG(fmt, ...) ((void)0)
    #else
        #define DEBUG_LOG(fmt, ...) do { \
            fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
            fflush(stderr); \
        } while(0)
    #endif
#endif