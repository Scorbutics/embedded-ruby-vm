#ifndef INSTALL_H
#define INSTALL_H

#include "embedded-ruby-vm/assets-error.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of native libraries that can be tracked.
 */
#define MAX_NATIVE_LIBS 32

/**
 * Native library information structure.
 */
typedef struct {
    char name[64];              /**< Library filename (e.g., "libruby.so.3.1") */
    uint32_t crc32;             /**< CRC32 checksum */
    size_t size;                /**< File size in bytes */
    char relative_path[256];    /**< Path relative to native_libs_dir */
} NativeLibInfo;

/**
 * Complete asset layout structure describing where all installed assets are located.
 * This is the single source of truth for asset paths after installation.
 */
typedef struct {
    char ruby_stdlib_path[512];       /**< Base install directory (e.g., "./test-ruby-install") - Ruby VM appends /ruby/<version>/ */
    char ruby_stdlib_ext_path[512];   /**< Path to platform-specific extensions */
    char native_libs_dir[512];        /**< Directory containing native .so files */
    int native_lib_count;             /**< Number of native libraries */
    NativeLibInfo native_libs[MAX_NATIVE_LIBS]; /**< Array of native library info */
    char platform[64];                /**< Platform identifier (e.g., "x86_64-linux-linux") */
    char version[128];                /**< Installation version string */
} AssetsLayout;

/**
 * Install all embedded files to the specified directory.
 *
 * This function will:
 * 1. Extract the Ruby standard library ZIP to <install_dir>/ruby/
 * 2. Extract platform-specific Ruby extensions
 * 3. Extract native libraries (if embedded) to <install_dir>/native-libs/{platform}/
 * 4. Write a manifest file with version and CRC32 checksums
 *
 * Note: fifo_interpreter.rb is now embedded in libembedded-ruby.so and
 * is no longer extracted to disk.
 *
 * @param install_dir The directory where files should be installed
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return ASSETS_OK (0) on success, negative AssetsErrorCode on error
 */
int install_embedded_files(const char *install_dir, AssetsError* error);

/**
 * Set the Android app data directory for use as the installation base.
 *
 * This function should be called early during Android initialization (via JNI)
 * to specify where the app's internal data directory is located.
 * Once set, get_default_install_dir() will automatically use this path.
 *
 * This is typically set to context.getFilesDir().getAbsolutePath() on Android.
 *
 * @param dir The Android app data directory path
 */
void set_android_app_data_dir(const char* dir);

/**
 * Get a default installation directory.
 *
 * Returns a platform-appropriate directory for installing files:
 * - On Android: Uses the app data directory set via set_android_app_data_dir()
 * - On other platforms: Uses $XDG_CACHE_HOME/embedded-ruby-vm if available
 * - Falls back to $HOME/.cache/embedded-ruby-vm
 * - Falls back to /tmp/embedded-ruby-vm as last resort
 *
 * @return Pointer to static string containing default install directory
 */
const char* get_default_install_dir(void);

/**
 * Check if installation is needed.
 *
 * Checks if the key files already exist in the install directory and
 * verifies their integrity using CRC32 checksums and version information.
 *
 * @param install_dir Directory to check
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return 1 if installation is needed, 0 if files already exist and are valid,
 *         negative AssetsErrorCode on error
 */
int installation_needed(const char *install_dir, AssetsError* error);

/**
 * Get the complete asset layout after installation.
 *
 * This function reads the installation manifest and returns a structure
 * describing where all installed assets are located. This is the single
 * source of truth for asset paths - Kotlin/Java code should use this
 * instead of guessing paths.
 *
 * The returned structure must be freed using assets_free_layout() when done.
 *
 * @param install_dir The installation directory
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return Pointer to AssetsLayout structure, or NULL on error
 */
AssetsLayout* assets_get_layout(const char* install_dir, AssetsError* error);

/**
 * Free an AssetsLayout structure returned by assets_get_layout().
 *
 * @param layout The layout structure to free (can be NULL)
 */
void assets_free_layout(AssetsLayout* layout);

/**
 * Validate that all assets in the layout exist and match their checksums.
 *
 * This performs a complete validation of the installation:
 * - Verifies all files exist
 * - Checks file sizes match manifest
 * - Validates CRC32 checksums (optional, slower)
 *
 * @param layout The asset layout to validate
 * @param verify_checksums If non-zero, compute and verify CRC32 checksums
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return ASSETS_OK (0) if validation succeeds, negative AssetsErrorCode on error
 */
int assets_validate_layout(const AssetsLayout* layout, int verify_checksums, AssetsError* error);

/**
 * Register external native libraries in the manifest.
 *
 * This function is used when native libraries are NOT embedded in the binary,
 * but exist in an external directory. It scans the directory, computes checksums,
 * and updates the manifest.
 *
 * This is part of the "hybrid approach" where C manages coordination but
 * libraries remain external to avoid binary bloat.
 *
 * @param install_dir The installation directory
 * @param external_lib_dir Directory containing the platform's .so files
 * @param platform Platform identifier (e.g., "x86_64-linux-linux")
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return ASSETS_OK (0) on success, negative AssetsErrorCode on error
 */
int assets_register_native_libs(const char* install_dir,
                                 const char* external_lib_dir,
                                 const char* platform,
                                 AssetsError* error);

/**
 * Bootstrap the Ruby runtime environment.
 *
 * This is a high-level convenience function that performs the complete
 * initialization sequence:
 *   1. Checks if asset extraction is needed
 *   2. Extracts embedded assets if needed (Ruby stdlib, native libs)
 *   3. Retrieves the asset layout
 *   4. Dynamically loads native libraries in dependency order
 *
 * This function encapsulates the two-phase loading pattern required for
 * the split-library architecture (libassets.so + libembedded-ruby.so).
 *
 * After calling this function successfully, you can use the returned layout
 * to create a Ruby interpreter with the correct paths.
 *
 * Example usage:
 * @code
 *   AssetsError error = {0};
 *   AssetsLayout* layout = assets_bootstrap("./ruby-install", &error);
 *   if (layout == NULL) {
 *       fprintf(stderr, "Bootstrap failed: %s\n", error.message);
 *       return -1;
 *   }
 *
 *   // Use layout->ruby_stdlib_path, layout->native_libs_dir, etc.
 *   // to create Ruby interpreter...
 *
 *   assets_free_layout(layout);
 * @endcode
 *
 * @param install_dir Directory where assets should be extracted
 * @param error Optional error structure to populate on failure (can be NULL)
 * @return Pointer to AssetsLayout on success, NULL on error
 */
AssetsLayout* assets_bootstrap(const char* install_dir, AssetsError* error);

#ifdef __cplusplus
}
#endif

#endif // INSTALL_H
