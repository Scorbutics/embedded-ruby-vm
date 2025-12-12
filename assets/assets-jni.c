/**
 * Minimal JNI bindings for asset extraction.
 * This file provides only the extraction functionality needed before
 * the main Ruby VM library is loaded.
 *
 * IMPORTANT: This code must NOT depend on libruby.so or any Ruby VM functionality.
 * It only depends on the assets library (install.c) and minizip (statically linked).
 */

#include <jni.h>
#include <string.h>
#include "install.h"
#include "assets-error.h"

/**
 * Extract embedded files to the specified directory.
 *
 * This function is called before loading libembedded-ruby.so to ensure
 * native libraries (libruby.so, libssl.so, etc.) are extracted and available.
 *
 * @param installDir The directory where files should be extracted
 * @return 0 on success, negative error code on failure
 */
JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_extractEmbeddedFiles(
    JNIEnv *env,
    jobject obj,
    jstring installDir
) {
    (void)obj; // Unused parameter

    if (installDir == NULL) {
        return ASSETS_ERROR_INVALID_PARAM;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (install_dir_str == NULL) {
        return ASSETS_ERROR_MEMORY;
    }

    // Call extraction function
    AssetsError error = {0};
    int result = install_embedded_files(install_dir_str, &error);

    // Release the string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    return (jint)result;
}

/**
 * Check if installation is needed.
 *
 * @param installDir The directory to check
 * @return 1 if installation needed, 0 if up-to-date, negative on error
 */
JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_installationNeeded(
    JNIEnv *env,
    jobject obj,
    jstring installDir
) {
    (void)obj; // Unused parameter

    if (installDir == NULL) {
        return ASSETS_ERROR_INVALID_PARAM;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (install_dir_str == NULL) {
        return ASSETS_ERROR_MEMORY;
    }

    // Check if installation is needed
    AssetsError error = {0};
    int result = installation_needed(install_dir_str, &error);

    // Release the string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    return (jint)result;
}

/**
 * Get the default installation directory.
 *
 * @return The default install directory path, or NULL on error
 */
JNIEXPORT jstring JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_getDefaultInstallDir(
    JNIEnv *env,
    jobject obj
) {
    (void)obj; // Unused parameter

    const char *install_dir = get_default_install_dir();
    if (install_dir == NULL) {
        return NULL;
    }

    return (*env)->NewStringUTF(env, install_dir);
}

/**
 * Bootstrap the Ruby runtime environment (high-level convenience function).
 *
 * This performs the complete initialization sequence:
 *   1. Checks if extraction is needed
 *   2. Extracts embedded assets if needed
 *   3. Gets the asset layout
 *   4. Loads native libraries in dependency order
 *
 * Returns a JSON string containing the asset layout paths, or NULL on error.
 * The JSON format is:
 * {
 *   "ruby_stdlib_path": "/path/to/ruby/3.1.0",
 *   "native_libs_dir": "/path/to/native-libs/x86_64-linux-linux",
 *   "platform": "x86_64-linux-linux"
 * }
 *
 * @param installDir The directory where files should be extracted
 * @return JSON string with layout paths, or NULL on error
 */
JNIEXPORT jstring JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_bootstrap(
    JNIEnv *env,
    jobject obj,
    jstring installDir
) {
    (void)obj; // Unused parameter

    if (installDir == NULL) {
        return NULL;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (install_dir_str == NULL) {
        return NULL;
    }

    // Call bootstrap function
    AssetsError error = {0};
    AssetsLayout* layout = assets_bootstrap(install_dir_str, &error);

    // Release the input string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    if (layout == NULL) {
        // Bootstrap failed - could throw exception here
        return NULL;
    }

    // Build JSON response
    char json_buffer[2048];
    snprintf(json_buffer, sizeof(json_buffer),
        "{"
        "\"ruby_stdlib_path\":\"%s\","
        "\"native_libs_dir\":\"%s\","
        "\"platform\":\"%s\""
        "}",
        layout->ruby_stdlib_path,
        layout->native_libs_dir,
        layout->platform
    );

    // Free the layout
    assets_free_layout(layout);

    // Convert to Java string
    return (*env)->NewStringUTF(env, json_buffer);
}

/**
 * Get the native libraries directory path from the installation.
 *
 * This function retrieves the path to the directory containing extracted
 * native libraries (.so files). The path is managed by the C layer.
 *
 * @param env JNI environment
 * @param obj Object reference (unused)
 * @param installDir Installation directory path
 * @return String path to native libs directory, or NULL on error
 */
JNIEXPORT jstring JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_getNativeLibsPath(
    JNIEnv *env,
    jobject obj,
    jstring installDir
) {
    (void)obj; // Unused parameter

    if (!installDir) {
        return NULL;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (!install_dir_str) {
        return NULL;
    }

    // Get asset layout from C
    AssetsError error = {0};
    AssetsLayout* layout = assets_get_layout(install_dir_str, &error);

    // Release the input string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    if (!layout) {
        return NULL;
    }

    // Create Java string from native libs directory
    jstring result = (*env)->NewStringUTF(env, layout->native_libs_dir);

    // Cleanup
    assets_free_layout(layout);

    return result;
}

/**
 * Get the count of extracted native libraries.
 *
 * @param env JNI environment
 * @param obj Object reference (unused)
 * @param installDir Installation directory path
 * @return Number of native libraries, or -1 on error
 */
JNIEXPORT jint JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_getNativeLibCount(
    JNIEnv *env,
    jobject obj,
    jstring installDir
) {
    (void)obj; // Unused parameter

    if (!installDir) {
        return -1;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (!install_dir_str) {
        return -1;
    }

    // Get asset layout
    AssetsError error = {0};
    AssetsLayout* layout = assets_get_layout(install_dir_str, &error);

    // Release the input string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    if (!layout) {
        return -1;
    }

    jint count = layout->native_lib_count;

    // Cleanup
    assets_free_layout(layout);

    return count;
}

/**
 * Get the full path to a specific native library by index.
 *
 * @param env JNI environment
 * @param obj Object reference (unused)
 * @param installDir Installation directory path
 * @param index Index of the library to get (0-based)
 * @return Full path to the library file, or NULL if index is invalid
 */
JNIEXPORT jstring JNICALL
Java_com_scorbutics_rubyvm_AssetsNative_getNativeLibPath(
    JNIEnv *env,
    jobject obj,
    jstring installDir,
    jint index
) {
    (void)obj; // Unused parameter

    if (!installDir || index < 0) {
        return NULL;
    }

    // Convert Java string to C string
    const char *install_dir_str = (*env)->GetStringUTFChars(env, installDir, NULL);
    if (!install_dir_str) {
        return NULL;
    }

    // Get asset layout
    AssetsError error = {0};
    AssetsLayout* layout = assets_get_layout(install_dir_str, &error);

    // Release the input string
    (*env)->ReleaseStringUTFChars(env, installDir, install_dir_str);

    if (!layout) {
        return NULL;
    }

    // Check if index is valid
    if (index >= layout->native_lib_count) {
        assets_free_layout(layout);
        return NULL;
    }

    // Build full path: native_libs_dir + "/" + relative_path
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s",
             layout->native_libs_dir,
             layout->native_libs[index].relative_path);

    jstring result = (*env)->NewStringUTF(env, full_path);

    // Cleanup
    assets_free_layout(layout);

    return result;
}
