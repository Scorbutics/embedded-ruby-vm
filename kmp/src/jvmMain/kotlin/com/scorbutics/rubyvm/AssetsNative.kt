package com.scorbutics.rubyvm

/**
 * JNI native method declarations for the assets library.
 * This library contains ONLY asset extraction functionality and has
 * NO dependencies on Ruby native libraries.
 *
 * IMPORTANT: This must be loaded BEFORE the main embedded-ruby library
 * to extract native dependencies (libruby.so, libssl.so, etc.)
 */
internal object AssetsNative {
    /**
     * Extract all embedded files to the specified directory.
     *
     * @param installDir The directory where files should be extracted
     * @return 0 on success, negative error code on failure
     */
    external fun extractEmbeddedFiles(installDir: String): Int

    /**
     * Check if installation/extraction is needed.
     *
     * @param installDir The directory to check
     * @return 1 if installation needed, 0 if up-to-date, negative on error
     */
    external fun installationNeeded(installDir: String): Int

    /**
     * Get the default installation directory.
     *
     * @return The default install directory path
     */
    external fun getDefaultInstallDir(): String?

    /**
     * Bootstrap the Ruby runtime environment (high-level convenience function).
     *
     * This performs the complete initialization sequence:
     *   1. Checks if extraction is needed
     *   2. Extracts embedded assets if needed
     *   3. Gets the asset layout
     *   4. Loads native libraries in dependency order
     *
     * @param installDir The directory where files should be extracted
     * @return JSON string with layout paths, or null on error
     */
    external fun bootstrap(installDir: String): String?

    /**
     * Get the native library directory path from the C-managed asset layout.
     *
     * @param installDir The installation directory
     * @return Path to the native libs directory, or null on error
     */
    external fun getNativeLibsPath(installDir: String): String?

    /**
     * Set the Android app data directory for asset installation.
     *
     * This function informs the native layer about the Android app's internal
     * data directory. The directory will be automatically used by the C code
     * when determining where to install Ruby VM assets.
     *
     * This should be called early during initialization on Android platforms,
     * typically with context.filesDir.absolutePath
     *
     * @param dir The Android app data directory path
     */
    external fun setAndroidAppDataDir(dir: String)

    /**
     * Get the count of extracted native libraries.
     *
     * @param installDir The installation directory
     * @return Number of native libraries, or -1 on error
     */
    external fun getNativeLibCount(installDir: String): Int

    /**
     * Get the full path to a specific native library by index.
     *
     * @param installDir The installation directory
     * @param index The library index (0-based, from 0 to count-1)
     * @return Full path to the library file, or null if index is invalid
     */
    external fun getNativeLibPath(installDir: String, index: Int): String?

    /**
     * Flag to track if the assets library has been loaded
     */
    @Volatile
    private var loaded = false

    /**
     * Load the assets native library.
     * This library contains only extraction code and has no Ruby dependencies.
     * Supports both static (.a) and shared (.so/.dylib) library builds.
     */
    @Synchronized
    fun loadLibrary() {
        if (loaded) {
            return
        }

        val platform = detectPlatform()
        val sharedFileName = mapLibraryName("assets")
        val sharedResourcePath = "/natives/$platform/$sharedFileName"

        try {
            // Try to load shared library from JAR
            val inputStream = AssetsNative::class.java.getResourceAsStream(sharedResourcePath)

            if (inputStream != null) {
                NativeLibraryLoaderHelper.loadFromStream(inputStream, sharedFileName)
                loaded = true
                println("✓ Loaded shared assets library from JAR: $sharedResourcePath")
            } else {
                // Fallback to System.loadLibrary (requires java.library.path)
                // This covers both:
                // - Static builds (library already linked, System.loadLibrary is a no-op)
                // - External shared libraries (loaded from java.library.path)
                try {
                    System.loadLibrary("assets")
                    loaded = true
                    println("✓ Loaded assets library from system path (may be static or shared)")
                } catch (e: UnsatisfiedLinkError) {
                    // If System.loadLibrary fails, assume it's a static build (already linked)
                    loaded = true
                    println("✓ Assuming static build - assets library already linked at compile time")
                }
            }
        } catch (e: Exception) {
            throw RuntimeException(
                "Failed to load assets library for platform '$platform'.\n" +
                "Tried resource: $sharedResourcePath\n" +
                "Error: ${e.message}", e
            )
        }
    }

    /**
     * Detect the current platform (OS + architecture).
     */
    private fun detectPlatform(): String {
        val os = System.getProperty("os.name").lowercase()
        val arch = System.getProperty("os.arch").lowercase()

        val osName = when {
            os.contains("win") -> "windows"
            os.contains("mac") -> "macos"
            os.contains("linux") -> "linux"
            else -> throw UnsupportedOperationException("Unsupported OS: $os")
        }

        val archName = when {
            arch.contains("amd64") || arch.contains("x86_64") -> "x64"
            arch.contains("aarch64") || arch.contains("arm64") -> "arm64"
            arch.contains("arm") -> "arm"
            else -> throw UnsupportedOperationException("Unsupported architecture: $arch")
        }

        return "$osName-$archName"
    }

    /**
     * Map library name to platform-specific file name.
     */
    private fun mapLibraryName(name: String): String {
        val os = System.getProperty("os.name").lowercase()
        return when {
            os.contains("win") -> "$name.dll"
            os.contains("mac") -> "lib$name.dylib"
            else -> "lib$name.so"
        }
    }
}
