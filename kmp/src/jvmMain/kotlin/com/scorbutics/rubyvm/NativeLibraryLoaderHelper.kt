package com.scorbutics.rubyvm

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.Files

/**
 * Shared helper for JVM-based platforms (Desktop and Android).
 * Contains common logic for asset extraction and native library loading.
 */
internal object NativeLibraryLoaderHelper {

    /**
     * Perform Phase 1 loading: Extract assets and load Ruby dependencies.
     * Returns the installation directory where files were extracted.
     *
     * @param getInstallDir Function to get the platform-specific installation directory
     * @return The installation directory containing extracted files
     */
    fun performPhase1Loading(getInstallDir: () -> File): File {
        println("=== Phase 1: Loading assets library ===")
        
        // If this library, nothing else is needed. This is a fat library.
        val fatLibName = LibraryConfig.libraryName
        try {
            System.loadLibrary(fatLibName)
            println("  ✓ Loaded: " + fatLibName)
        } catch (e: UnsatisfiedLinkError) {
            println("  ⚠ Skipped " + fatLibName + ": ${e.message}")
        }

        AssetsNative.loadLibrary()

        // Determine install directory for extracted files
        val installDir = getInstallDir()
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

        return installDir
    }

    /**
     * Load extracted native libraries in dependency order.
     * These must be loaded before libembedded-ruby.so.
     */
    fun loadExtractedNativeLibraries(installDir: File) {
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
            "libruby.so"         // Depends on everything above
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
     * Perform Phase 2 loading: Load the main embedded-ruby library.
     *
     * @param libraryName The name of the main library to load
     * @return true if loaded as shared library, false if static build
     */
    fun performPhase2Loading(libraryName: String): Boolean {
        println("=== Phase 2: Loading main Ruby VM library ===")
        val platform = detectPlatform()
        val sharedFileName = mapLibraryName(libraryName)
        val sharedResourcePath = "/natives/$platform/$sharedFileName"

        try {
            // Try to load shared library from JAR/APK
            val inputStream = NativeLibraryLoaderHelper::class.java.getResourceAsStream(sharedResourcePath)

            if (inputStream != null) {
                loadFromStream(inputStream, sharedFileName)
                println("✓ Loaded shared embedded-ruby library from resources: $sharedResourcePath")
                return false // Not a static build
            } else {
                // Fallback to System.loadLibrary (requires java.library.path)
                // This covers both:
                // - Static builds (library already linked, System.loadLibrary is a no-op)
                // - External shared libraries (loaded from java.library.path)
                try {
                    System.loadLibrary(libraryName)
                    println("✓ Loaded embedded-ruby library from system path (may be static or shared)")
                } catch (e: UnsatisfiedLinkError) {
                    // If System.loadLibrary fails, assume it's a static build (already linked)
                    println("✓ Assuming static build - library already linked at compile time")
                }
                return true // May be static build
            }
        } catch (e: Exception) {
            throw RuntimeException(
                "Failed to load native library '$libraryName' for platform '$platform'.\n" +
                "Tried resource: $sharedResourcePath\n" +
                "Error: ${e.message}", e
            )
        }
    }

    /**
     * Extract library from input stream to temp file and load it.
     */
    fun loadFromStream(inputStream: InputStream, libraryFileName: String) {
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
     * Detect platform string as used by the C code (e.g., "x86_64-linux-linux", "aarch64-android")
     */
    fun detectPlatformString(): String {
        val arch = System.getProperty("os.arch").lowercase()
        val os = System.getProperty("os.name").lowercase()

        val archName = when {
            arch.contains("amd64") || arch.contains("x86_64") -> "x86_64"
            arch.contains("aarch64") || arch.contains("arm64") -> "aarch64"
            arch.contains("arm") && !arch.contains("64") -> "arm"
            arch.contains("x86") && !arch.contains("64") -> "x86"
            else -> arch
        }

        // Detect if running on Android
        val isAndroid = try {
            Class.forName("android.os.Build")
            true
        } catch (e: ClassNotFoundException) {
            false
        }

        val osName = when {
            isAndroid -> "android"
            os.contains("linux") -> "linux-linux"
            os.contains("mac") -> "darwin"
            os.contains("win") -> "windows"
            else -> "unknown"
        }

        return "$archName-$osName"
    }

    /**
     * Detect the current platform (OS + architecture) for resource path.
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
