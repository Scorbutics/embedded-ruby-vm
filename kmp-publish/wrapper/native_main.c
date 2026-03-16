/*
 * native_main.c
 *
 * Generic main() entry point for Android NativeActivity with SFML.
 *
 * When librgss_runtime.so is loaded by a NativeActivity, SFML's
 * ANativeActivity_onCreate (from sfml-main.a) sets up the EGL context
 * and spawns a thread that calls main(). This file provides that main()
 * function, which reads configuration from environment variables and
 * executes a Ruby script directly on the calling thread (inline).
 *
 * Running Ruby inline on the SFML thread is critical: SFML prepares an
 * ALooper on this thread for event processing. If Ruby ran on a separate
 * VM thread (as in the multi-threaded KMP path), SFML's ALooper_pollAll
 * would fail with "No looper for this thread!" on every frame.
 *
 * Environment variables (set by Kotlin LauncherActivity before starting NativeActivity):
 *   RGSS_RUBY_BASE_DIR   - Path to Ruby standard library
 *   RGSS_NATIVE_LIBS_DIR - Path to native extension libraries
 *   RGSS_SCRIPT_PATH     - Path to the Ruby script file to execute
 *
 * In regular Activity usage (not NativeActivity), this main() is never called.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#define TAG "RGSSMain"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#else
#define LOGI(...) do { printf("[RGSSMain] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[RGSSMain ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

/* Inline execution: runs Ruby directly on the calling thread */
extern int ExecRubyScriptInline(const char* rubyDirectoryPath,
                                const char* nativeLibsDirLocation,
                                const char* scriptFilePath);

/* Logging API */
typedef enum {
    LOG_STREAM_RUBY_STDOUT_M = 1,
    LOG_STREAM_RUBY_STDERR_M = 2,
    LOG_STREAM_VMLOGGER_M = 3,
    LOG_STREAM_NATIVE_STDOUT_M = 4,
    LOG_STREAM_NATIVE_STDERR_M = 5
} log_stream_main_t;

extern int logging_init(const char* appname);
extern int logging_add_custom_output(
    int (*func)(const char* line, log_stream_main_t stream, void* context),
    void* context);

/* Log callback for the logging system */
static int on_log_message(const char* line, log_stream_main_t stream, void* context) {
    (void)context;
    switch (stream) {
        case LOG_STREAM_RUBY_STDERR_M:
        case LOG_STREAM_NATIVE_STDERR_M:
            LOGE("[Ruby] %s", line);
            break;
        default:
            LOGI("[Ruby] %s", line);
            break;
    }
    return 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    LOGI("NativeActivity main() started");

    /* Read configuration from environment variables */
    const char* ruby_base_dir = getenv("RGSS_RUBY_BASE_DIR");
    const char* native_libs_dir = getenv("RGSS_NATIVE_LIBS_DIR");
    const char* script_path = getenv("RGSS_SCRIPT_PATH");

    if (!ruby_base_dir || !native_libs_dir || !script_path) {
        LOGE("Missing environment variables:");
        LOGE("  RGSS_RUBY_BASE_DIR=%s", ruby_base_dir ? ruby_base_dir : "(not set)");
        LOGE("  RGSS_NATIVE_LIBS_DIR=%s", native_libs_dir ? native_libs_dir : "(not set)");
        LOGE("  RGSS_SCRIPT_PATH=%s", script_path ? script_path : "(not set)");
        return 1;
    }

    LOGI("Ruby base dir: %s", ruby_base_dir);
    LOGI("Native libs dir: %s", native_libs_dir);
    LOGI("Script path: %s", script_path);

    /* Set up logging */
    logging_init("com.scorbutics.rubyvm");
    logging_add_custom_output(on_log_message, NULL);

    /* Execute Ruby script inline on this thread (the SFML thread with ALooper) */
    LOGI("Executing Ruby script inline on SFML thread...");
    int result = ExecRubyScriptInline(ruby_base_dir, native_libs_dir, script_path);
    LOGI("Script execution finished with result: %d", result);

    LOGI("NativeActivity main() exiting");
    return result;
}
