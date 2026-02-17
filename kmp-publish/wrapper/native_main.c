/*
 * native_main.c
 *
 * Generic main() entry point for Android NativeActivity with SFML.
 *
 * When librgss_runtime.so is loaded by a NativeActivity, SFML's
 * ANativeActivity_onCreate (from sfml-main.a) sets up the EGL context
 * and spawns a thread that calls main(). This file provides that main()
 * function, which reads configuration from environment variables and
 * executes a Ruby script using the embedded Ruby VM.
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

/* Forward declarations matching embedded-ruby-vm C API */
struct RubyInterpreter;
struct RubyScript;
struct LogListener;

typedef void (*LogAcceptFunc)(struct LogListener* listener, const char* lineMessage);
typedef void (*LogErrorFunc)(struct LogListener* listener, const char* errorMessage);

typedef enum {
    LOG_STREAM_RUBY_STDOUT_M = 1,
    LOG_STREAM_RUBY_STDERR_M = 2,
    LOG_STREAM_VMLOGGER_M = 3,
    LOG_STREAM_NATIVE_STDOUT_M = 4,
    LOG_STREAM_NATIVE_STDERR_M = 5
} log_stream_main_t;

typedef void (*LogMessageFunc)(struct LogListener* listener, const char* message, log_stream_main_t source);

typedef struct LogListener {
    void* context;
    void* user_data;
    LogAcceptFunc accept;
    LogErrorFunc on_log_error;
    LogMessageFunc on_log_message;
} LogListener;

/* Extern declarations for Ruby C API functions (linked from fat library) */
extern struct RubyInterpreter* ruby_interpreter_create(
    const char* application_path,
    const char* ruby_base_directory,
    const char* native_libs_location,
    LogListener listener
);
extern void ruby_interpreter_destroy(struct RubyInterpreter* interpreter);
extern int ruby_interpreter_execute_sync(struct RubyInterpreter* interpreter, struct RubyScript* script);
extern int ruby_interpreter_enable_logging(struct RubyInterpreter* interpreter);

extern struct RubyScript* ruby_script_create_from_content(const char* content, size_t content_size);
extern void ruby_script_destroy(struct RubyScript* script);

/* Log callbacks */
static void on_log(struct LogListener* listener, const char* message) {
    LOGI("[Ruby] %s", message);
}

static void on_log_error(struct LogListener* listener, const char* message) {
    LOGE("[Ruby] %s", message);
}

static void on_log_message(struct LogListener* listener, const char* message, log_stream_main_t source) {
    switch (source) {
        case LOG_STREAM_RUBY_STDERR_M:
        case LOG_STREAM_NATIVE_STDERR_M:
            LOGE("[Ruby] %s", message);
            break;
        default:
            LOGI("[Ruby] %s", message);
            break;
    }
}

/* Read entire file into a malloc'd buffer */
static char* read_file_contents(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)size, f);
    buffer[read] = '\0';
    fclose(f);
    return buffer;
}

int main(int argc, char** argv) {
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

    /* Read the Ruby script file */
    char* script_content = read_file_contents(script_path);
    if (!script_content) {
        LOGE("Failed to read script file: %s", script_path);
        return 1;
    }

    LOGI("Script loaded (%zu bytes)", strlen(script_content));

    /* Set up log listener */
    LogListener listener;
    memset(&listener, 0, sizeof(listener));
    listener.accept = on_log;
    listener.on_log_error = on_log_error;
    listener.on_log_message = on_log_message;

    /* Create Ruby interpreter */
    struct RubyInterpreter* interpreter = ruby_interpreter_create(
        ".", ruby_base_dir, native_libs_dir, listener
    );

    if (!interpreter) {
        LOGE("Failed to create Ruby interpreter");
        free(script_content);
        return 1;
    }

    ruby_interpreter_enable_logging(interpreter);

    /* Create and execute the script */
    struct RubyScript* script = ruby_script_create_from_content(
        script_content, strlen(script_content)
    );

    if (!script) {
        LOGE("Failed to create Ruby script object");
        ruby_interpreter_destroy(interpreter);
        free(script_content);
        return 1;
    }

    LOGI("Executing Ruby rendering script...");
    int result = ruby_interpreter_execute_sync(interpreter, script);
    LOGI("Script execution finished with result: %d", result);

    /* Cleanup */
    ruby_script_destroy(script);
    ruby_interpreter_destroy(interpreter);
    free(script_content);

    LOGI("NativeActivity main() exiting");
    return result;
}
