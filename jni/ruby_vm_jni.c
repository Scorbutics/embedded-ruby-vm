#include <stdlib.h>
#include <string.h>
#include <logging.h>

#include <android/log.h>

#include "env.h"
#include "ruby_vm_jni.h"
#include "ruby-vm.h"
#include "ruby-script-location.h"
#include "ruby-script.h"
#include "ruby-interpreter.h"
#include "completion-task.h"

// Completion callback context
typedef struct {
    JavaVM* jvm;
    jobject callback_obj;
    jmethodID invoke_method_id;
} CompletionCallbackContext;

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

    JNIEnv* env;
    jint result = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, use daemon attachment for automatic cleanup
        if ((*jvm)->AttachCurrentThreadAsDaemon(jvm, &env, NULL) == JNI_OK) {
            return env;
        }
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to attach thread as daemon");
        return NULL;
    } else if (result == JNI_OK) {
        // Already attached
        return env;
    }

    __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "GetEnv failed with unknown error");
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Invalid parameters to create_jni_callback_context");
        return NULL;
    }

    JNICallbackContext* context = malloc(sizeof(JNICallbackContext));
    if (!context) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to allocate JNI callback context");
        return NULL;
    }

    // Get JavaVM for later use in callbacks from native threads
    if ((*env)->GetJavaVM(env, &context->jvm) != JNI_OK) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get JavaVM");
        free(context);
        return NULL;
    }

    // Create global reference to Kotlin listener object
    // This prevents the object from being garbage collected
    context->kotlin_listener = (*env)->NewGlobalRef(env, kotlin_listener);
    if (!context->kotlin_listener) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to create global reference");
        free(context);
        return NULL;
    }

    // Get the class and cache method IDs for better performance
    jclass listener_class = (*env)->GetObjectClass(env, kotlin_listener);
    if (!listener_class) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get listener class");
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
        free(context);
        return NULL;
    }

    // Get method IDs for the LogListener interface methods
    context->accept_method_id = (*env)->GetMethodID(env, listener_class,
                                                    "accept", "(Ljava/lang/String;)V");
    context->error_method_id = (*env)->GetMethodID(env, listener_class,
                                                   "onLogError", "(Ljava/lang/String;)V");

    (*env)->DeleteLocalRef(env, listener_class);

    if (!context->accept_method_id || !context->error_method_id) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get method IDs");
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
        free(context);
        return NULL;
    }

    return context;
}

/**
 * Destroy JNI callback context and clean up resources.
 * Safe to call from any thread.
 */
static void destroy_jni_callback_context(JNICallbackContext* context) {
    if (!context) return;

    JNIEnv* env;
    jint result = (*context->jvm)->GetEnv(context->jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, need to attach temporarily to delete global ref
        if ((*context->jvm)->AttachCurrentThread(context->jvm, &env, NULL) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, context->kotlin_listener);
            (*context->jvm)->DetachCurrentThread(context->jvm);
        }
    } else if (result == JNI_OK) {
        // Already attached, just delete the reference
        (*env)->DeleteGlobalRef(env, context->kotlin_listener);
    }

    free(context);
}

// ============================================================================
// Log Callbacks (Thread-safe without mutexes - context passed as parameter)
// ============================================================================

/**
 * C callback for log messages.
 * Called from native Ruby VM threads, context passed directly.
 * No global state access - completely thread-safe.
 */
static void jni_log_accept_callback(LogListener* listener, const char* message) {
    JNICallbackContext* context = (JNICallbackContext*) listener->context;

    // Get JNI environment for current thread
    JNIEnv* env = get_jni_env(context->jvm);
    if (!env) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get JNI env in log accept");
        return;
    }

    // Convert C string to Java string
    jstring j_message = (*env)->NewStringUTF(env, message);
    if (j_message) {
        // Call the Kotlin accept method
        (*env)->CallVoidMethod(env, context->kotlin_listener,
                               context->accept_method_id, j_message);

        // Clean up local reference
        (*env)->DeleteLocalRef(env, j_message);
    }

    // Check for exceptions and log them
    if ((*env)->ExceptionCheck(env)) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Exception in log accept callback");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    // No need to detach - daemon threads auto-detach
}

/**
 * C callback for log errors.
 * Called from native Ruby VM threads, context passed directly.
 * No global state access - completely thread-safe.
 */
static void jni_log_error_callback(LogListener* listener, const char* error_message) {
    JNICallbackContext* context = (JNICallbackContext*) listener->context;

    // Get JNI environment for current thread
    JNIEnv* env = get_jni_env(context->jvm);
    if (!env) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get JNI env in log error");
        return;
    }

    // Convert C string to Java string
    jstring j_error_message = (*env)->NewStringUTF(env, error_message);
    if (j_error_message) {
        // Call the Kotlin onLogError method
        (*env)->CallVoidMethod(env, context->kotlin_listener,
                               context->error_method_id, j_error_message);

        // Clean up local reference
        (*env)->DeleteLocalRef(env, j_error_message);
    }

    // Check for exceptions and log them
    if ((*env)->ExceptionCheck(env)) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Exception in log error callback");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    // No need to detach - daemon threads auto-detach
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Invalid parameters to create_completion_context");
        return NULL;
    }

    CompletionCallbackContext* context = malloc(sizeof(CompletionCallbackContext));
    if (!context) {
        *errorCode = 2;
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to allocate completion context");
        return NULL;
    }

    // Get JavaVM for later use in callbacks
    if ((*env)->GetJavaVM(env, &context->jvm) != JNI_OK) {
        free(context);
        *errorCode = 3;
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get JavaVM for completion");
        return NULL;
    }

    // Create global reference to callback object
    context->callback_obj = (*env)->NewGlobalRef(env, completion_callback);
    if (!context->callback_obj) {
        free(context);
        *errorCode = 4;
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to create global ref for completion");
        return NULL;
    }

    // Get the class and method ID for the callback
    jclass callback_class = (*env)->GetObjectClass(env, completion_callback);
    if (!callback_class) {
        (*env)->DeleteGlobalRef(env, context->callback_obj);
        free(context);
        *errorCode = 5;
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get completion callback class");
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get complete method ID");
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

    JNIEnv* env;
    jint result = (*context->jvm)->GetEnv(context->jvm, (void**)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        // Not attached, need to attach temporarily to delete global ref
        if ((*context->jvm)->AttachCurrentThread(context->jvm, &env, NULL) == JNI_OK) {
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Completion callback called with NULL context");
        return;
    }

    CompletionCallbackContext* context = (CompletionCallbackContext*)user_context;

    // Get JNI environment for current thread
    JNIEnv* env = get_jni_env(context->jvm);
    if (!env) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to get JNI env in completion callback");
        destroy_completion_context(context);
        return;
    }

    // Call the Kotlin callback function with the result
    (*env)->CallVoidMethod(env, context->callback_obj,
                           context->invoke_method_id, (jint)result);

    // Check for exceptions
    if ((*env)->ExceptionCheck(env)) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Exception in completion callback");
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to convert path strings");
        free(c_app_path);
        free(c_ruby_base_directory);
        free(c_native_libs_directory);
        return 0;
    }

    // Set up Android logging
    LoggingSetNativeLoggingFunction(__android_log_write);

    // Create JNI callback context for log listener
    JNICallbackContext* callback_context = create_jni_callback_context(env, kotlin_listener);
    if (!callback_context) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to create JNI callback context");
        free(c_app_path);
        free(c_ruby_base_directory);
        free(c_native_libs_directory);
        return 0; // null pointer
    }

    // Create LogListener with C callback functions and context
    // The context is stored IN the LogListener and will be passed to callbacks
    LogListener listener = {
            .context = callback_context,
            .user_data = NULL,
            .accept = jni_log_accept_callback,
            .on_log_error = jni_log_error_callback
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to create Ruby interpreter");
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
        __android_log_write(ANDROID_LOG_WARN, "RubyVM", "Attempting to destroy NULL interpreter");
        return;
    }

    RubyInterpreter* interpreter = (RubyInterpreter*)interpreter_ptr;

    // Get the callback context from the interpreter before destroying it
    // Assuming your ruby_interpreter_get_log_context() or similar exists
    // If not, you'll need to store the context pointer separately and pass it here
    LogListener* listener = &interpreter->log_listener;
    JNICallbackContext* callback_context = NULL;

    if (listener) {
        callback_context = (JNICallbackContext*)listener->context;
    }

    // Destroy the interpreter first
    ruby_interpreter_destroy(interpreter);

    // Then clean up the callback context
    if (callback_context) {
        destroy_jni_callback_context(callback_context);
    }
}

JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_createScript(JNIEnv *env, jclass clazz,
                                                     jstring content) {
    (void) clazz;

    char* c_content = jstring_to_cstring(env, content);
    if (!c_content) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to convert script content");
        return 0;
    }

    RubyScript* script = ruby_script_create_from_content(c_content, strlen(c_content));
    free(c_content);

    if (!script) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to create Ruby script");
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
        __android_log_write(ANDROID_LOG_WARN, "RubyVM", "Attempting to destroy NULL script");
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
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Invalid interpreter or script pointer");

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
            __android_log_print(ANDROID_LOG_ERROR, "RubyVM",
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
        __android_log_print(ANDROID_LOG_ERROR, "RubyVM",
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

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_updateEnvLocations(JNIEnv *env, jclass clazz,
                                                           jstring current_directory,
                                                           jstring extra_arg) {
    (void) clazz;

    char* c_current_directory = jstring_to_cstring(env, current_directory);
    char* c_extra_arg = jstring_to_cstring(env, extra_arg);

    if (!c_current_directory || !c_extra_arg) {
        __android_log_write(ANDROID_LOG_ERROR, "RubyVM", "Failed to convert env location strings");
        free(c_current_directory);
        free(c_extra_arg);
        return -1;
    }

    const int result = env_update_locations(c_current_directory, c_extra_arg);

    free(c_extra_arg);
    free(c_current_directory);

    return result;
}
