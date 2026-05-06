// ========== JNI Bridge Header (ruby_vm_jni.h) ==========
#ifndef RUBY_VM_JNI_H
#define RUBY_VM_JNI_H

#include <jni.h>
#include <stdint.h>

// Magic canaries for detecting use-after-free of JNICallbackContext.
// LIVE  = set when context is created and valid for callback dispatch.
// DEAD  = stamped on destroy(); if a callback ever sees this, the context
//         was freed but the global VM still references it (UAF).
// Other = freed memory was reused by a different allocation; magic is now
//         arbitrary garbage and the context must not be dereferenced.
#define JNI_CALLBACK_CONTEXT_MAGIC_LIVE 0xC4118ACEU
#define JNI_CALLBACK_CONTEXT_MAGIC_DEAD 0xDEADBEEFU

// JNI callback context structure
// IMPORTANT: keep `magic` first so callbacks can probe it before touching jvm.
typedef struct {
    uint32_t magic;
    JavaVM* jvm;
    jobject kotlin_listener; // Global reference to Kotlin LogListener
    jmethodID accept_method_id;       // Legacy: accept(String)
    jmethodID error_method_id;        // Legacy: onLogError(String)
    jmethodID log_message_method_id;  // New: onLogMessage(String, int)
} JNICallbackContext;

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_updateEnvLocations(JNIEnv *env, jclass clazz, jstring current_directory, jstring extra_arg);

// JNI function declarations
JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_createInterpreter(JNIEnv *env, jclass clazz,
                                                     jstring app_path,
                                                     jstring ruby_base_directory,
                                                     jstring native_libs_location,
                                                     jobject kotlin_listener);

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_destroyInterpreter(JNIEnv *env, jclass clazz,
                                                      jlong interpreter_ptr);

JNIEXPORT jlong JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_createScript(JNIEnv *env, jclass clazz,
                                                jstring content);

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_destroyScript(JNIEnv *env, jclass clazz,
                                                 jlong script_ptr);

JNIEXPORT void JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enqueueScript(JNIEnv *env, jclass clazz,
                                                 jlong interpreter_ptr,
                                                 jlong script_ptr,
                                                 jobject completion_callback);

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_enableLogging(JNIEnv *env, jclass clazz,
                                                                jlong interpreter_ptr);

JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_RubyVMNative_disableLogging(JNIEnv *env, jclass clazz,
                                                                jlong interpreter_ptr);
#endif // RUBY_VM_JNI_H
