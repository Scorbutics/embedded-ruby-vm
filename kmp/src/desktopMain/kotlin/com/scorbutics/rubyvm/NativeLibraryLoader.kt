package com.scorbutics.rubyvm

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.Files

/**
 * Desktop implementation of native library loader.
 * Implements two-phase loading to solve circular dependency:
 *
 * Phase 1: Load libassets.so (no dependencies)
 *          Extract embedded native libraries (libruby.so, etc.)
 *          Load extracted libruby.so
 *
 * Phase 2: Load libembedded-ruby.so (depends on libassets.so and libruby.so)
 *
 * This solves the bootstrap problem where libembedded-ruby.so requires libruby.so,
 * but libruby.so is embedded in libassets.so.
 */
internal actual object NativeLibraryLoader {
    private var loaded = false
    private val libraryName = "embedded-ruby"
    private var extractedLibsDir: File? = null

    /**
     * Load the native libraries for the current platform.
     * Uses two-phase loading to handle circular dependencies.
     */
    @Synchronized
    actual fun loadLibrary() {
        if (loaded) {
            return
        }

        // PHASE 1: Load assets library and extract native dependencies
        println("=== Phase 1: Loading assets library ===")
        AssetsNative.loadLibrary()

        // Determine install directory for extracted files
        val installDir = getInstallDirectory()
        println("Install directory: $installDir")

        // Check if extraction is needed
        val needsInstall = AssetsNative.installationNeeded(installDir.absolutePath)
        if (needsInstall == 1) {
            println("Extracting embedded native libraries...")
            val extractResult = AssetsNative.extractEmbeddedFiles(installDir.absolutePath)
            if (extractResult != 0) {
                throw RuntimeException("Failed to extract embedded files. Error code: $extractResult")
            }
            println("✓ Native libraries extracted successfully")
        } else if (needsInstall == 0) {
            println("✓ Native libraries already up-to-date")
        } else {
            throw RuntimeException("Failed to check installation status. Error code: $needsInstall")
        }

        // Load extracted native libraries explicitly
        println("Loading extracted Ruby native libraries...")
        loadExtractedNativeLibraries(installDir)

        // PHASE 2: Load main embedded-ruby library
        println("=== Phase 2: Loading main Ruby VM library ===")
        val platform = detectPlatform()
        val libraryFileName = mapLibraryName(libraryName)
        val resourcePath = "/natives/$platform/$libraryFileName"

        try {
            // Try to load from embedded resources
            val inputStream = NativeLibraryLoader::class.java.getResourceAsStream(resourcePath)

            if (inputStream != null) {
                loadFromStream(inputStream, libraryFileName)
                loaded = true
                println("✓ Loaded embedded-ruby library from JAR: $resourcePath")
            } else {
                // Fallback to System.loadLibrary (requires java.library.path)
                System.loadLibrary(libraryName)
                loaded = true
                println("✓ Loaded embedded-ruby library from system path")
            }
        } catch (e: Exception) {
            throw RuntimeException(
                "Failed to load native library '$libraryName' for platform '$platform'.\n" +
                "Tried resource: $resourcePath\n" +
                "Error: ${e.message}", e
            )
        }

        println("=== Native library loading complete ===")
    }

    /**
     * Get the installation directory for extracted native libraries.
     */
    private fun getInstallDirectory(): File {
        // Try to use the default from C code first
        val defaultDir = AssetsNative.getDefaultInstallDir()
        if (defaultDir != null) {
            val dir = File(defaultDir)
            dir.mkdirs()
            return dir
        }

        // Fallback to a temp directory in user's home
        val userHome = System.getProperty("user.home")
        val cacheDir = File(userHome, ".cache/embedded-ruby-vm")
        cacheDir.mkdirs()
        return cacheDir
    }

    /**
     * Load extracted native libraries in dependency order.
     * These must be loaded before libembedded-ruby.so.
     */
    private fun loadExtractedNativeLibraries(installDir: File) {
        val platform = detectPlatform()
        val platformDir = detectPlatformString()

        // Native libs are extracted to: <installDir>/native-libs/<platform>/
        val nativeLibsDir = File(installDir, "native-libs/$platformDir")

        if (!nativeLibsDir.exists()) {
            println("Warning: Native libs directory not found: ${nativeLibsDir.absolutePath}")
            return
        }

        // Load libraries in dependency order (dependencies first)
        val libsToLoad = listOf(
            "libncurses.so",      // Base library
            "libpanel.so",        // Depends on ncurses
            "libmenu.so",         // Depends on ncurses
            "libform.so",         // Depends on ncurses
            "libcurses.so",       // May be symlink to ncurses
            "libgdbm.so",         // Base library
            "libgdbm_compat.so",  // Depends on gdbm
            "libhistory.so",      // Base library
            "libreadline.so",     // Depends on history, ncurses
            "libcrypto.so",       // Base library
            "libssl.so",          // Depends on crypto
            "libruby.so"          // Depends on everything above
        )

        for (libName in libsToLoad) {
            val libFile = File(nativeLibsDir, libName)
            if (libFile.exists()) {
                try {
                    System.load(libFile.absolutePath)
                    println("  ✓ Loaded: $libName")
                } catch (e: UnsatisfiedLinkError) {
                    // Some libraries might be optional or already loaded
                    println("  ⚠ Skipped $libName: ${e.message}")
                }
            }
        }
    }

    /**
     * Detect platform string as used by the C code (e.g., "x86_64-linux-linux")
     */
    private fun detectPlatformString(): String {
        val arch = System.getProperty("os.arch").lowercase()
        val os = System.getProperty("os.name").lowercase()

        val archName = when {
            arch.contains("amd64") || arch.contains("x86_64") -> "x86_64"
            arch.contains("aarch64") || arch.contains("arm64") -> "aarch64"
            else -> arch
        }

        val osName = when {
            os.contains("linux") -> "linux-linux"
            os.contains("mac") -> "darwin"
            os.contains("win") -> "windows"
            else -> "unknown"
        }

        return "$archName-$osName"
    }

    /**
     * Extract library from input stream to temp file and load it.
     * Made internal so AssetsNative can use it.
     */
    internal fun loadFromStream(inputStream: InputStream, libraryFileName: String) {
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
