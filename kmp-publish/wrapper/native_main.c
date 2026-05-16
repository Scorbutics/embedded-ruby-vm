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
 *   RGSS_RUBY_BASE_DIR        - Path to Ruby standard library
 *   RGSS_NATIVE_LIBS_DIR      - Path to native extension libraries
 *   RGSS_SCRIPT_PATH          - Path to the Ruby script file to execute
 *
 * Optional remote-listener configuration (debug builds only); if a *_PORT
 * and *_TOKEN pair is set, the corresponding listener is armed at boot:
 *   RGSS_REMOTE_DEBUG_HOST    - Bind address (default 127.0.0.1)
 *   RGSS_REMOTE_DEBUG_PORT    - TCP port for the rdbg/DAP listener
 *   RGSS_REMOTE_DEBUG_TOKEN   - Shared-secret cookie
 *   RGSS_REMOTE_DEBUG_SESSION - Optional friendly label
 *   RGSS_REMOTE_EVAL_HOST     - Bind address (default 127.0.0.1)
 *   RGSS_REMOTE_EVAL_PORT     - TCP port for the line-eval console
 *   RGSS_REMOTE_EVAL_TOKEN    - Shared-secret cookie
 *   RGSS_REMOTE_EVAL_SESSION  - Optional friendly label
 *
 * In regular Activity usage (not NativeActivity), this main() is never called.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public option-struct types only — no pthread/atomic/VM internals.
 * The include path for embedded-ruby-vm/ comes from the wrapper's
 * CMakeLists target_include_directories pointing at include/public/. */
#include "embedded-ruby-vm/remote-options.h"

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

extern int ExecRubyScriptInlineWithRemote(const char* rubyDirectoryPath,
                                          const char* nativeLibsDirLocation,
                                          const char* scriptFilePath,
                                          const RubyVMRemoteDebugOptions* remote_debug,
                                          const RubyVMRemoteEvalOptions* remote_eval);

/* Logging API */
typedef enum {
    LOG_STREAM_RUBY_STDOUT_M = 1,
    LOG_STREAM_RUBY_STDERR_M = 2,
    LOG_STREAM_VMLOGGER_M = 3,
    LOG_STREAM_NATIVE_STDOUT_M = 4,
    LOG_STREAM_NATIVE_STDERR_M = 5
} log_stream_main_t;

/* Mirror of log_level_t (log-listener.h). Kept inline so this standalone
 * wrapper doesn't need the embedded-ruby-vm headers. */
typedef enum {
    LOG_LEVEL_DEBUG_M = 1,
    LOG_LEVEL_INFO_M  = 2,
    LOG_LEVEL_ERROR_M = 3
} log_level_main_t;

extern int logging_init(const char* appname);
extern int logging_add_custom_output(
    int (*func)(const char* line, log_stream_main_t stream, log_level_main_t level, void* context),
    void* context);

/* Log callback for the logging system. Routes by parsed severity so
 * VMLogger.error reaches the error log channel even though it shares the
 * vmlogger pipe with .info/.debug. */
static int on_log_message(const char* line, log_stream_main_t stream, log_level_main_t level, void* context) {
    (void)stream;
    (void)context;
    if (level == LOG_LEVEL_ERROR_M) {
        LOGE("[Ruby] %s", line);
    } else {
        LOGI("[Ruby] %s", line);
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

    /* Build optional remote-listener options from env vars. A listener is
     * considered "requested" only when both PORT and TOKEN are present —
     * we never bind a port without a cookie, even on loopback (matches the
     * JNI enableRemote* contract).
     *
     * Note: opts.host falls back to NULL → the C layer defaults to
     * "127.0.0.1", which is what we want for adb forward-style debugging. */
    const char* debug_port_s   = getenv("RGSS_REMOTE_DEBUG_PORT");
    const char* debug_token    = getenv("RGSS_REMOTE_DEBUG_TOKEN");
    const char* debug_host     = getenv("RGSS_REMOTE_DEBUG_HOST");
    const char* debug_session  = getenv("RGSS_REMOTE_DEBUG_SESSION");
    const char* eval_port_s    = getenv("RGSS_REMOTE_EVAL_PORT");
    const char* eval_token     = getenv("RGSS_REMOTE_EVAL_TOKEN");
    const char* eval_host      = getenv("RGSS_REMOTE_EVAL_HOST");
    const char* eval_session   = getenv("RGSS_REMOTE_EVAL_SESSION");

    RubyVMRemoteDebugOptions debug_opts;
    RubyVMRemoteEvalOptions  eval_opts;
    const RubyVMRemoteDebugOptions* debug_ptr = NULL;
    const RubyVMRemoteEvalOptions*  eval_ptr  = NULL;

    if (debug_port_s && debug_token && debug_token[0] != '\0') {
        debug_opts.host         = debug_host;
        debug_opts.port         = atoi(debug_port_s);
        debug_opts.token        = debug_token;
        debug_opts.session_name = debug_session;
        debug_ptr = &debug_opts;
        LOGI("Remote DAP debugger requested on %s:%d",
             debug_host ? debug_host : "127.0.0.1", debug_opts.port);
    }
    if (eval_port_s && eval_token && eval_token[0] != '\0') {
        eval_opts.host         = eval_host;
        eval_opts.port         = atoi(eval_port_s);
        eval_opts.token        = eval_token;
        eval_opts.session_name = eval_session;
        eval_ptr = &eval_opts;
        LOGI("Remote eval console requested on %s:%d",
             eval_host ? eval_host : "127.0.0.1", eval_opts.port);
    }

    /* Execute Ruby script inline on this thread (the SFML thread with ALooper) */
    LOGI("Executing Ruby script inline on SFML thread...");
    int result = ExecRubyScriptInlineWithRemote(ruby_base_dir, native_libs_dir, script_path,
                                                debug_ptr, eval_ptr);
    LOGI("Script execution finished with result: %d", result);

    LOGI("NativeActivity main() exiting");
    return result;
}
