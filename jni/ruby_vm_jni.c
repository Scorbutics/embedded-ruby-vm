#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "embedded-ruby-vm/env.h"
#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/jni_logging.h"
#include "embedded-ruby-vm/ruby_vm_jni.h"
#include "embedded-ruby-vm/ruby-vm.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-script.h"
#include "embedded-ruby-vm/ruby-interpreter.h"
#include "embedded-ruby-vm/completion-task.h"
#include "embedded-ruby-vm/debug.h"

/*
 * Diagnostic log helper that bypasses the logging pipe.
 *
 * Regular DEBUG_LOG / fprintf(stderr) is captured by the logging system's
 * stderr redirect and dispatched back through jni_log_*_callback. After we
 * stamp a context as DEAD inside destroy_jni_callback_context, those queued
 * messages get rejected by the magic check — so any DESTROY log written
 * during destruction is silently dropped. Routing diagnostic output to the
 * pre-redirect terminal FD avoids that loop entirely.
 */
static void diag_log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';

    int fd = logging_get_original_stderr_fd();
    if (fd < 0) fd = 2; /* logging not yet redirected — plain stderr is fine */
    ssize_t ignored = write(fd, buf, (size_t)n);
    (void)ignored;
}

/* Process-global counter incremented every time a JNI log callback observes
 * a non-LIVE magic on its JNICallbackContext — i.e. a use-after-free that
 * was caught by the canary. Tests assert this counter does not advance
 * during their execution to detect any regression of the listener-lifetime
 * race fixed by logging_swap_listener. */
static atomic_uint_fast64_t g_bad_magic_count = 0;

/*
 * Cross-platform JNI thread attachment helpers.
 *
 * Android's jni.h declares AttachCurrentThread(JavaVM*, JNIEnv**, void*),
 * while the standard JNI spec (OpenJDK/desktop) uses (JavaVM*, void**, void*).
 *
 * On Android we can pass &env directly.  On other platforms we go through a
 * void* intermediary so every pointer conversion is well-defined C (no
 * void** <-> T** aliasing).
 */
#ifdef __ANDROID__
#define JNI_ATTACH_THREAD(jvm, env, args) \
    (*jvm)->AttachCurrentThread(jvm, &(env), args)
#define JNI_ATTACH_THREAD_AS_DAEMON(jvm, env, args) \
    (*jvm)->AttachCurrentThreadAsDaemon(jvm, &(env), args)
#else
#define JNI_ATTACH_THREAD(jvm, env, args) \
    ({ void* _jni_env_void = NULL; \
       jint _jni_rc = (*(jvm))->AttachCurrentThread((jvm), &_jni_env_void, (args)); \
       if (_jni_rc == JNI_OK) (env) = (JNIEnv*)_jni_env_void; \
       _jni_rc; })
#define JNI_ATTACH_THREAD_AS_DAEMON(jvm, env, args) \
    ({ void* _jni_env_void = NULL; \
       jint _jni_rc = (*(jvm))->AttachCurrentThreadAsDaemon((jvm), &_jni_env_void, (args)); \
       if (_jni_rc == JNI_OK) (env) = (JNIEnv*)_jni_env_void; \
       _jni_rc; })
#endif

// Forward declarations
static JNIEnv* get_jni_env(JavaVM* jvm);



// Completion callback context
typedef struct {
    JavaVM* jvm;
    jobject callback_obj;
    jmethodID invoke_method_id;
} CompletionCallbackContext;

// Script completion sentinel synchronization is now handled centrally
// by the logging system. See logging_reset_sentinel() / logging_wait_for_sentinel().

// ============================================================================
// JNI Environment Helpers
// ============================================================================

/**
 * Safely get JNIEnv for the current thread.
 * Uses AttachCurrentThreadAsDaemon for automatic cleanup.
 *
 * @param jvm The JavaVM instance
 * @return JNIEnv pointer or NULL on failure
 */
static JNIEnv* get_jni_env(JavaVM* jvm) {
    if (!jvm) return NULL;

    JNIEnv* env = NULL;
    jint result = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, use daemon attachment for automatic cleanup
        if (JNI_ATTACH_THREAD_AS_DAEMON(jvm, env, NULL) == JNI_OK) {
            return env;
        }
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to attach thread as daemon");
        return NULL;
    } else if (result == JNI_OK) {
        // Already attached
        return env;
    }

    jni_log_write(JNI_LOG_ERROR, "RubyVM", "GetEnv failed with unknown error");
    return NULL;
}

/**
 * Helper to convert jstring to C string.
 * Caller must free the returned string.
 */
static char* jstring_to_cstring(JNIEnv* env, jstring j_str) {
    if (!j_str) return NULL;

    const char* str = (*env)->GetStringUTFChars(env, j_str, NULL);
    if (!str) return NULL;

    char* c_str = strdup(str);
    (*env)->ReleaseStringUTFChars(env, j_str, str);

    return c_str;
}

// ============================================================================
// Log Callback Context Management
// ============================================================================

/**
 * Create a JNI callback context for log listeners.
 * This context will be passed through the LogListener structure.
 */
static JNICallbackContext* create_jni_callback_context(JNIEnv* env, jobject kotlin_listener) {
    if (!env || !kotlin_listener) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Invalid parameters to create_jni_callback_context");
        return NULL;
    }

    JNICallbackContext* context = malloc(sizeof(JNICallbackContext));
    if (!context) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to allocate JNI callback context");
        return NULL;
    }

    context->magic = JNI_CALLBACK_CONTEXT_MAGIC_LIVE;

    // Get JavaVM for later use in callbacks from native threads
    if ((*env)->GetJavaVM(env, &context->jvm) != JNI_OK) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get JavaVM");
        free(context);
        return NULL;
    }

    // Create global reference to Kotlin listener object
    // This prevents the object from being garbage collected
    context->kotlin_listener = (*env)->NewGlobalRef(env, kotlin_listener);
    if (!context->kotlin_listener) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create global reference");
        free(context);
        return NULL;
    }

    // Get the class and cache method IDs for better performance
    jclass listener_class = (*env)->GetObjectClass(env, kotlin_listener);
    if (!listener_class) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get listener class");
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
        free(context);
        return NULL;
    }

    // Cache the single Kotlin callback method ID.
    // Signature: (message: String, source: Int, level: Int, interpreterId: Int) -> Unit
    // The trailing int is the per-interpreter routing id parsed by the
    // C dispatcher from the in-band TaggedIO tag; LOG_NATIVE_INTERPRETER_ID
    // for native/untagged lines.
    context->log_message_method_id = (*env)->GetMethodID(env, listener_class,
                                                         "onLogMessage", "(Ljava/lang/String;III)V");

    (*env)->DeleteLocalRef(env, listener_class);

    if (!context->log_message_method_id) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get onLogMessage method ID");
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
        free(context);
        return NULL;
    }

    diag_log("[JNI-DIAG] CREATE ctx=%p magic=0x%08x jvm=%p listener=%p",
              (void*)context, context->magic, (void*)context->jvm,
              (void*)context->kotlin_listener);

    return context;
}

/**
 * Destroy JNI callback context and clean up resources.
 * Safe to call from any thread.
 */
static void destroy_jni_callback_context(JNICallbackContext* context) {
    if (!context) return;

    diag_log("[JNI-DIAG] DESTROY entry ctx=%p magic=0x%08x jvm=%p listener=%p",
              (void*)context, context->magic, (void*)context->jvm,
              (void*)context->kotlin_listener);

    if (context->magic != JNI_CALLBACK_CONTEXT_MAGIC_LIVE) {
        diag_log("[JNI-DIAG] DESTROY ctx=%p magic mismatch (got 0x%08x, expected 0x%08x) — double free or corruption!",
                  (void*)context, context->magic, JNI_CALLBACK_CONTEXT_MAGIC_LIVE);
    }

    JNIEnv* env = NULL;
    jint result = (*context->jvm)->GetEnv(context->jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, need to attach temporarily to delete global ref
        if (JNI_ATTACH_THREAD(context->jvm, env, NULL) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, context->kotlin_listener);
            (*context->jvm)->DetachCurrentThread(context->jvm);
        }
    } else if (result == JNI_OK) {
        // Already attached, just delete the reference
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
    }

    // Stamp dead magic and clear pointers so a UAF from the logging thread
    // is detected (magic check) rather than crashing on the freed jvm pointer.
    context->magic = JNI_CALLBACK_CONTEXT_MAGIC_DEAD;
    context->jvm = NULL;
    context->kotlin_listener = NULL;

    diag_log("[JNI-DIAG] DESTROY freeing ctx=%p", (void*)context);
    free(context);
}

// ============================================================================
// Log Callbacks (Thread-safe without mutexes - context passed as parameter)
// ============================================================================

/**
 * C callback for log messages with source + severity.
 * Called from the native logging thread via LogListener — that thread is
 * daemon-attached so the JNI call is safe.
 */
static void jni_log_message_callback(LogListener* listener, const char* message, log_stream_t source, log_level_t level, int interpreter_id) {
    // Validate listener pointer
    if (!listener) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "jni_log_message_callback: NULL listener");
        return;
    }

    // Validate message pointer
    if (!message) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "jni_log_message_callback: NULL message");
        return;
    }

    // Validate context pointer
    JNICallbackContext* context = (JNICallbackContext*) listener->context;
    if (!context) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "jni_log_message_callback: NULL context");
        return;
    }

    if (context->magic != JNI_CALLBACK_CONTEXT_MAGIC_LIVE) {
        atomic_fetch_add(&g_bad_magic_count, 1);
        diag_log("[JNI-DIAG] message_callback: BAD magic listener=%p ctx=%p magic=0x%08x jvm=%p source=%d level=%d interp=%d — UAF detected, dropping",
                  (void*)listener, (void*)context, context->magic, (void*)context->jvm, (int)source, (int)level, interpreter_id);
        return;
    }

    // Get JNI environment for this thread (attaches as daemon if needed)
    JNIEnv* env = get_jni_env(context->jvm);
    if (!env) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get JNI env in log message callback");
        jni_log_printf(JNI_LOG_DEBUG, "RubyVM", "Message was: %s", message);
        return;
    }

    // Create Java String for message content
    jstring j_message = (*env)->NewStringUTF(env, message);
    if (!j_message) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create Java string from message");
        return;
    }

    // Call the Kotlin onLogMessage method with source + parsed VMLogger severity
    // + interpreter_id parsed from the in-band tag by the C dispatch (or
    // LOG_NATIVE_INTERPRETER_ID for native lines).
    (*env)->CallVoidMethod(env, context->kotlin_listener,
                            context->log_message_method_id, j_message,
                            (jint)source, (jint)level, (jint)interpreter_id);

    // Clean up
    (*env)->DeleteLocalRef(env, j_message);

    // Check for exceptions
    if ((*env)->ExceptionCheck(env)) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Exception in log message callback");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
}

// ============================================================================
// Completion Callback Context Management
// ============================================================================

/**
 * Create a completion callback context.
 *
 * @param env JNI environment
 * @param completion_callback Java callback object
 * @param errorCode Output parameter for error code (0 = success)
 * @return CompletionCallbackContext or NULL on failure
 */
static CompletionCallbackContext* create_completion_context(JNIEnv* env, jobject completion_callback, int* errorCode) {
    if (!env || !completion_callback) {
        *errorCode = 1;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Invalid parameters to create_completion_context");
        return NULL;
    }

    CompletionCallbackContext* context = malloc(sizeof(CompletionCallbackContext));
    if (!context) {
        *errorCode = 2;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to allocate completion context");
        return NULL;
    }

    // Get JavaVM for later use in callbacks
    if ((*env)->GetJavaVM(env, &context->jvm) != JNI_OK) {
        free(context);
        *errorCode = 3;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get JavaVM for completion");
        return NULL;
    }

    // Create global reference to callback object
    context->callback_obj = (*env)->NewGlobalRef(env, completion_callback);
    if (!context->callback_obj) {
        free(context);
        *errorCode = 4;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create global ref for completion");
        return NULL;
    }

    // Get the class and method ID for the callback
    jclass callback_class = (*env)->GetObjectClass(env, completion_callback);
    if (!callback_class) {
        (*env)->DeleteGlobalRef(env, context->callback_obj);
        free(context);
        *errorCode = 5;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get completion callback class");
        return NULL;
    }

    // Look for complete method that takes an int parameter
    context->invoke_method_id = (*env)->GetMethodID(env, callback_class,
                                                    "complete", "(I)V");

    (*env)->DeleteLocalRef(env, callback_class);

    if (!context->invoke_method_id) {
        (*env)->DeleteGlobalRef(env, context->callback_obj);
        free(context);
        *errorCode = 6;
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get complete method ID");
        return NULL;
    }

    *errorCode = 0;
    return context;
}

/**
 * Destroy completion callback context and clean up resources.
 * Safe to call from any thread.
 */
static void destroy_completion_context(CompletionCallbackContext* context) {
    if (!context) return;

    JNIEnv* env = NULL;
    jint result = (*context->jvm)->GetEnv(context->jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, need to attach temporarily to delete global ref
        if (JNI_ATTACH_THREAD(context->jvm, env, NULL) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, context->callback_obj);
            (*context->jvm)->DetachCurrentThread(context->jvm);
        }
    } else if (result == JNI_OK) {
        // Already attached, just delete the reference
        (*env)->DeleteGlobalRef(env, context->callback_obj);
    }

    free(context);
}

// ============================================================================
// Completion Callback (Thread-safe without mutexes - context passed as parameter)
// ============================================================================

/**
 * C completion callback called from Ruby VM thread.
 * Context is passed directly - no global state, completely thread-safe.
 *
 * @param user_context CompletionCallbackContext passed from enqueueScript
 * @param result The completion result code (0 = success, non-zero = error)
 */
static void jni_completion_callback(void* user_context, int result) {
    if (!user_context) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Completion callback called with NULL context");
        return;
    }

    CompletionCallbackContext* context = (CompletionCallbackContext*)user_context;

    // Get JNI environment for current thread
    JNIEnv* env = get_jni_env(context->jvm);
    if (!env) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to get JNI env in completion callback");
        destroy_completion_context(context);
        return;
    }

    // Call the Kotlin callback function with the result
    (*env)->CallVoidMethod(env, context->callback_obj,
                           context->invoke_method_id, (jint)result);

    // Check for exceptions
    if ((*env)->ExceptionCheck(env)) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Exception in completion callback");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    // Clean up the context after callback is complete
    // This is safe because the Ruby VM won't call this callback again for this task
    destroy_completion_context(context);

    // No need to detach - daemon threads auto-detach
}

// ============================================================================
// JNI Native Methods
// ============================================================================

JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_createInterpreter(JNIEnv *env, jclass clazz,
                                                          jstring app_path,
                                                          jstring ruby_base_directory,
                                                          jstring native_libs_directory,
                                                          jobject kotlin_listener) {
    (void) clazz;

    // Convert Java strings to C strings
    char* c_app_path = jstring_to_cstring(env, app_path);
    char* c_ruby_base_directory = jstring_to_cstring(env, ruby_base_directory);
    char* c_native_libs_directory = jstring_to_cstring(env, native_libs_directory);

    if (!c_app_path || !c_ruby_base_directory || !c_native_libs_directory) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to convert path strings");
        free(c_app_path);
        free(c_ruby_base_directory);
        free(c_native_libs_directory);
        return 0;
    }

    // Note: Native logging function should be set by user via setLogCallback

    // Create JNI callback context for log listener
    JNICallbackContext* callback_context = create_jni_callback_context(env, kotlin_listener);
    if (!callback_context) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create JNI callback context");
        free(c_app_path);
        free(c_ruby_base_directory);
        free(c_native_libs_directory);
        return 0; // null pointer
    }

    // Create LogListener with C callback functions and context.
    // The context is stored IN the LogListener and will be passed to callbacks.
    // The callback context construction already requires log_message_method_id,
    // so by here we know the Kotlin side has onLogMessage.
    LogListener listener = {
            .context = callback_context,
            .user_data = NULL,
            .on_log_message = jni_log_message_callback
    };

    // Create interpreter
    RubyInterpreter* interpreter = ruby_interpreter_create(
            c_app_path,
            c_ruby_base_directory,
            c_native_libs_directory,
            listener
    );

    // Clean up C strings
    free(c_ruby_base_directory);
    free(c_native_libs_directory);
    free(c_app_path);

    if (!interpreter) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create Ruby interpreter");
        destroy_jni_callback_context(callback_context);
        return 0;
    }

    return (jlong)interpreter;
}

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_destroyInterpreter(JNIEnv *env, jclass clazz,
                                                           jlong interpreter_ptr) {
    (void) env;
    (void) clazz;

    if (!interpreter_ptr) {
        jni_log_write(JNI_LOG_WARN, "RubyVM", "Attempting to destroy NULL interpreter");
        return;
    }

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    // Get the callback context from the interpreter before destroying it
    LogListener* listener = &interpreter->log_listener;
    JNICallbackContext* callback_context = NULL;

    if (listener) {
        callback_context = (JNICallbackContext*)listener->context;
    }

    diag_log("[JNI-DIAG] destroyInterpreter entry: interpreter=%p listener=%p ctx=%p",
              (void*)interpreter, (void*)listener, (void*)callback_context);

    // Destroy the interpreter first
    ruby_interpreter_destroy(interpreter);
    DEBUG_LOG("Interpreter destroyed");

    // Clean up the callback context
    if (callback_context) {
        destroy_jni_callback_context(callback_context);
    }
    DEBUG_LOG("JNI callback context destroyed");
}

JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_createScript(JNIEnv *env, jclass clazz,
                                                     jstring content) {
    (void) clazz;

    char* c_content = jstring_to_cstring(env, content);
    if (!c_content) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to convert script content");
        return 0;
    }

    RubyScript* script = ruby_script_create_from_content(c_content, strlen(c_content));
    free(c_content);

    if (!script) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to create Ruby script");
        return 0;
    }

    return (jlong)script;
}

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_destroyScript(JNIEnv *env, jclass clazz,
                                                      jlong script_ptr) {
    (void) env;
    (void) clazz;

    if (!script_ptr) {
        jni_log_write(JNI_LOG_WARN, "RubyVM", "Attempting to destroy NULL script");
        return;
    }

    RubyScript* script = (RubyScript*)script_ptr;
    ruby_script_destroy(script);
}

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enqueueScript(JNIEnv *env, jclass clazz,
                                                      jlong interpreter_ptr,
                                                      jlong script_ptr,
                                                      jobject completion_callback) {
    (void) clazz;

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;
    RubyScript* script = (RubyScript*)script_ptr;

    // Validate inputs
    if (!interpreter || !script) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Invalid interpreter or script pointer");

        // If callback exists, call it with error result immediately
        if (completion_callback) {
            jclass callback_class = (*env)->GetObjectClass(env, completion_callback);
            if (callback_class) {
                jmethodID complete_method = (*env)->GetMethodID(env, callback_class, "complete", "(I)V");
                if (complete_method) {
                    (*env)->CallVoidMethod(env, completion_callback, complete_method, (jint)1);
                }
                (*env)->DeleteLocalRef(env, callback_class);
            }
        }
        return;
    }

    CompletionCallbackContext* context = NULL;
    RubyCompletionCallback c_completion_callback = NULL;

    // Create completion callback context if callback is provided
    if (completion_callback) {
        int context_result;
        context = create_completion_context(env, completion_callback, &context_result);

        if (context) {
            // Successfully created context, use our callback
            c_completion_callback = jni_completion_callback;
        } else {
            // Failed to create context
            jni_log_printf(JNI_LOG_ERROR, "RubyVM",
                           "Failed to create completion context (error %d)", context_result);

            // Call callback with error immediately
            jclass callback_class = (*env)->GetObjectClass(env, completion_callback);
            if (callback_class) {
                jmethodID complete_method = (*env)->GetMethodID(env, callback_class, "complete", "(I)V");
                if (complete_method) {
                    (*env)->CallVoidMethod(env, completion_callback, complete_method, (jint)1);
                }
                (*env)->DeleteLocalRef(env, callback_class);
            }
            return;
        }
    }

    // Enqueue the script with the completion callback and context
    // The Ruby VM will call jni_completion_callback(context, result) when done
    const int interpreter_script_result = ruby_interpreter_enqueue(
            interpreter,
            script,
            ruby_completion_task_create(c_completion_callback, context)
    );

    if (interpreter_script_result != 0) {
        jni_log_printf(JNI_LOG_ERROR, "RubyVM",
                       "Failed to enqueue script (error %d)", interpreter_script_result);

        // If enqueue failed immediately, clean up context and notify callback
        if (context) {
            // Call the callback with error code
            JNIEnv* callback_env = get_jni_env(context->jvm);
            if (callback_env) {
                (*callback_env)->CallVoidMethod(callback_env, context->callback_obj,
                                                context->invoke_method_id, (jint)interpreter_script_result);

                if ((*callback_env)->ExceptionCheck(callback_env)) {
                    (*callback_env)->ExceptionDescribe(callback_env);
                    (*callback_env)->ExceptionClear(callback_env);
                }
            }

            // Clean up the context
            destroy_completion_context(context);
        }
    }

    // Note: If enqueue succeeded, the context will be cleaned up in jni_completion_callback
    // after the Ruby VM calls it
}

/**
 * Execute a script synchronously and return the result.
 * This method BLOCKS until the script completes.
 *
 * IMPORTANT: This should be called from a JVM thread (e.g., Kotlin's thread{}), NOT from
 * a native pthread. This avoids the JVM GC scanning native pthread stacks.
 *
 * @param interpreterPtr Pointer to the RubyInterpreter
 * @param scriptPtr Pointer to the RubyScript
 * @return 0 on success, non-zero error code on failure
 */
JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_executeScriptSync(JNIEnv *env, jclass clazz,
                                                           jlong interpreter_ptr,
                                                           jlong script_ptr) {
    (void) env;
    (void) clazz;

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;
    RubyScript* script = (RubyScript*)script_ptr;

    // Validate inputs
    if (!interpreter || !script) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Invalid interpreter or script pointer in executeScriptSync");
        return 1;  // Error
    }

    DEBUG_LOG("executeScriptSync: Executing script synchronously on JVM thread");

    // Reset the sentinel before execution
    jni_log_write(JNI_LOG_DEBUG, "RubyVM", "executeScriptSync: Resetting log flush sentinel");
    logging_reset_sentinel();

    // Execute the script synchronously on the calling (JVM) thread
    // This BLOCKS until the script completes and returns the result directly
    // No native pthread is created, so JVM GC won't scan native stacks!
    // This also handles VM initialization if needed
    const int result = ruby_interpreter_execute_sync(interpreter, script);

    DEBUG_LOG("executeScriptSync: Script execution completed with result: %d", result);

    // Wait for all logs to be flushed.
    // The logging system intercepts the sentinel on LOG_STREAM_VMLOGGER
    // (processed after RUBY_STDOUT and RUBY_STDERR), ensuring all
    // user-visible output has been dispatched to callbacks.
    jni_log_write(JNI_LOG_DEBUG, "RubyVM", "executeScriptSync: Waiting for log flush sentinel...");
    int wait_result = logging_wait_for_sentinel(5000);
    if (wait_result == 0) {
        jni_log_write(JNI_LOG_DEBUG, "RubyVM", "executeScriptSync: All logs flushed successfully");
    } else {
        jni_log_write(JNI_LOG_WARN, "RubyVM", "executeScriptSync: Timeout waiting for log flush");
    }

    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_updateEnvLocations(JNIEnv *env, jclass clazz,
                                                           jstring current_directory,
                                                           jstring extra_arg) {
    (void) clazz;

    char* c_current_directory = jstring_to_cstring(env, current_directory);
    char* c_extra_arg = jstring_to_cstring(env, extra_arg);

    if (!c_current_directory || !c_extra_arg) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "Failed to convert env location strings");
        free(c_current_directory);
        free(c_extra_arg);
        return -1;
    }

    const int result = env_update_locations(c_current_directory, c_extra_arg);

    free(c_extra_arg);
    free(c_current_directory);

    return result;
}

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enableLogging(JNIEnv *env, jclass clazz,
                                                      jlong interpreter_ptr) {
    (void) clazz;
    (void) env;

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    // Setup logging
    DEBUG_LOG("Enabling logging");
    int logging_result = ruby_interpreter_enable_logging(interpreter);
    if (logging_result != 0) {
        DEBUG_LOG("ruby_interpreter_enable_logging() failed with code: %d", logging_result);
        DEBUG_LOG("Error message: %s", ruby_interpreter_get_error_message(interpreter));
        return logging_result;
    }

    return 0;
}

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enableRemoteDebug(JNIEnv *env, jclass clazz,
                                                          jlong interpreter_ptr,
                                                          jstring host,
                                                          jint port,
                                                          jstring token,
                                                          jstring session_name) {
    (void) clazz;

    if (!interpreter_ptr) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "enableRemoteDebug: NULL interpreter");
        return RUBY_VM_ERROR_INVALID_PARAM;
    }
    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    char* c_host         = host         ? jstring_to_cstring(env, host)         : NULL;
    char* c_token        = token        ? jstring_to_cstring(env, token)        : NULL;
    char* c_session_name = session_name ? jstring_to_cstring(env, session_name) : NULL;

    if (token && !c_token) {
        // jstring_to_cstring failed for a required arg
        free(c_host);
        free(c_session_name);
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    RubyVMRemoteDebugOptions opts = {
        .host         = c_host,         // NULL is fine — C side defaults to 127.0.0.1
        .port         = (int)port,
        .token        = c_token,
        .session_name = c_session_name,
    };

    DEBUG_LOG("enableRemoteDebug: host=%s port=%d session=%s",
              c_host ? c_host : "(default 127.0.0.1)",
              (int)port,
              c_session_name ? c_session_name : "(none)");

    int result = ruby_interpreter_enable_remote_debug(interpreter, &opts);
    if (result != 0) {
        jni_log_printf(JNI_LOG_ERROR, "RubyVM", "enableRemoteDebug failed (code=%d): %s",
                       result, ruby_interpreter_get_error_message(interpreter));
    }

    free(c_host);
    free(c_token);
    free(c_session_name);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enableRemoteEval(JNIEnv *env, jclass clazz,
                                                         jlong interpreter_ptr,
                                                         jstring host,
                                                         jint port,
                                                         jstring token,
                                                         jstring session_name) {
    (void) clazz;

    if (!interpreter_ptr) {
        jni_log_write(JNI_LOG_ERROR, "RubyVM", "enableRemoteEval: NULL interpreter");
        return RUBY_VM_ERROR_INVALID_PARAM;
    }
    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    char* c_host         = host         ? jstring_to_cstring(env, host)         : NULL;
    char* c_token        = token        ? jstring_to_cstring(env, token)        : NULL;
    char* c_session_name = session_name ? jstring_to_cstring(env, session_name) : NULL;

    if (token && !c_token) {
        free(c_host);
        free(c_session_name);
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    RubyVMRemoteEvalOptions opts = {
        .host         = c_host,
        .port         = (int)port,
        .token        = c_token,
        .session_name = c_session_name,
    };

    DEBUG_LOG("enableRemoteEval: host=%s port=%d session=%s",
              c_host ? c_host : "(default 127.0.0.1)",
              (int)port,
              c_session_name ? c_session_name : "(none)");

    int result = ruby_interpreter_enable_remote_eval(interpreter, &opts);
    if (result != 0) {
        jni_log_printf(JNI_LOG_ERROR, "RubyVM", "enableRemoteEval failed (code=%d): %s",
                       result, ruby_interpreter_get_error_message(interpreter));
    }

    free(c_host);
    free(c_token);
    free(c_session_name);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_disableLogging(JNIEnv *env, jclass clazz,
                                                       jlong interpreter_ptr) {
    (void) clazz;
    (void) env;

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    // Disable logging
    DEBUG_LOG("Disabling logging");
    int logging_result = ruby_interpreter_disable_logging(interpreter);
    if (logging_result != 0) {
        DEBUG_LOG("ruby_interpreter_disable_logging() failed with code: %d", logging_result);
        DEBUG_LOG("Error message: %s", ruby_interpreter_get_error_message(interpreter));
        return logging_result;
    }

    return 0;
}

/**
 * Test-only: read the cumulative count of UAF events caught by the
 * JNICallbackContext magic canary. Tests bracket their bodies with
 * a baseline read and a delta == 0 assertion to detect any regression
 * of the listener-lifetime race.
 */
JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_getBadMagicCount(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    return (jlong)atomic_load(&g_bad_magic_count);
}

/**
 * Test-only: synchronously drain the logging system. Returns 0 once every
 * callback dispatch for data buffered before the call has completed; -1 on
 * timeout; -2 if logging is not running. See logging_drain().
 */
JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_drainLogging(JNIEnv* env, jclass clazz,
                                                     jlong timeout_ms) {
    (void)env; (void)clazz;
    return (jint)logging_drain((int)timeout_ms);
}
