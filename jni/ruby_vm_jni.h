// ========== JNI Bridge Header (ruby_vm_jni.h) ==========
#ifndef RUBY_VM_JNI_H
#define RUBY_VM_JNI_H

#include <jni.h>

// JNI callback context structure
typedef struct {
    JavaVM* jvm;
    jobject kotlin_listener; // Global reference to Kotlin LogListener
    jmethodID accept_method_id;
    jmethodID error_method_id;
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
#endif // RUBY_VM_JNI_H
