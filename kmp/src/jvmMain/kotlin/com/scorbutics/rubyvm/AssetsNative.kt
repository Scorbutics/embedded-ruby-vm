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
     * Flag to track if the assets library has been loaded
     */
    @Volatile
    private var loaded = false

    /**
     * Load the assets native library.
     * This library contains only extraction code and has no Ruby dependencies.
     */
    @Synchronized
    fun loadLibrary() {
        if (loaded) {
            return
        }

        val platform = detectPlatform()
        val libraryFileName = mapLibraryName("assets")
        val resourcePath = "/natives/$platform/$libraryFileName"

        try {
            // Try to load from embedded resources
            val inputStream = AssetsNative::class.java.getResourceAsStream(resourcePath)

            if (inputStream != null) {
                NativeLibraryLoader.loadFromStream(inputStream, libraryFileName)
                loaded = true
                println("✓ Loaded assets library from JAR: $resourcePath")
            } else {
                // Fallback to System.loadLibrary (requires java.library.path)
                System.loadLibrary("assets")
                loaded = true
                println("✓ Loaded assets library from system path")
            }
        } catch (e: Exception) {
            throw RuntimeException(
                "Failed to load assets library '$libraryFileName' for platform '$platform'.\n" +
                "Tried resource: $resourcePath\n" +
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
