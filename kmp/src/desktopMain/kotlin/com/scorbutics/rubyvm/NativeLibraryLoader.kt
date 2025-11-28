package com.scorbutics.rubyvm

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.Files

/**
 * Desktop implementation of native library loader.
 * Loads native libraries embedded in the JAR.
 *
 * This utility extracts platform-specific native libraries from the JAR's resources
 * to a temporary directory and loads them. This allows the JAR to be self-contained
 * without requiring users to set java.library.path.
 */
internal actual object NativeLibraryLoader {
    private var loaded = false
    private val libraryName = "embedded-ruby"

    /**
     * Load the native library for the current platform.
     * The library is extracted from JAR resources to a temp directory.
     */
    @Synchronized
    actual fun loadLibrary() {
        if (loaded) {
            return
        }

        val platform = detectPlatform()
        val libraryFileName = mapLibraryName(libraryName)
        val resourcePath = "/natives/$platform/$libraryFileName"

        try {
            // Try to load from embedded resources
            val inputStream = NativeLibraryLoader::class.java.getResourceAsStream(resourcePath)

            if (inputStream != null) {
                loadFromStream(inputStream, libraryFileName)
                loaded = true
                println("✓ Loaded native library from JAR: $resourcePath")
            } else {
                // Fallback to System.loadLibrary (requires java.library.path)
                System.loadLibrary(libraryName)
                loaded = true
                println("✓ Loaded native library from system path")
            }
        } catch (e: Exception) {
            throw RuntimeException(
                "Failed to load native library '$libraryName' for platform '$platform'.\n" +
                "Tried resource: $resourcePath\n" +
                "Also tried system library path (java.library.path).\n" +
                "Error: ${e.message}", e
            )
        }
    }

    /**
     * Extract library from input stream to temp file and load it.
     */
    private fun loadFromStream(inputStream: InputStream, libraryFileName: String) {
        // Create temp directory for native libraries
        val tempDir = Files.createTempDirectory("ruby-vm-native").toFile()
        tempDir.deleteOnExit()

        // Extract to temp file
        val tempFile = File(tempDir, libraryFileName)
        tempFile.deleteOnExit()

        inputStream.use { input ->
            FileOutputStream(tempFile).use { output ->
                input.copyTo(output)
            }
        }

        // Make executable (Linux/macOS)
        tempFile.setExecutable(true, false)
        tempFile.setReadable(true, false)

        // Load the extracted library
        System.load(tempFile.absolutePath)
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
