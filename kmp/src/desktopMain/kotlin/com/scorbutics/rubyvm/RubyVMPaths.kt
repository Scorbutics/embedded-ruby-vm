package com.scorbutics.rubyvm

import java.io.File

/**
 * Utility object to get paths for extracted Ruby VM runtime and native libraries.
 *
 * The Ruby VM automatically extracts its runtime files (Ruby standard library,
 * native libraries like libruby.so, etc.) to a cache directory on first use.
 *
 * Use this object to get the correct paths when creating a RubyInterpreter.
 *
 * Example:
 * ```kotlin
 * val paths = RubyVMPaths.getDefaultPaths()
 * RubyInterpreter.create(
 *     appPath = ".",
 *     rubyBaseDir = paths.rubyBaseDir,
 *     nativeLibsDir = paths.nativeLibsDir,
 *     listener = myListener
 * )
 * ```
 */
object RubyVMPaths {
    /**
     * Container for Ruby VM installation paths.
     */
    data class Paths(
        /** Base installation directory (e.g., "/home/user/.cache/embedded-ruby-vm") */
        val installDir: String,

        /** Ruby base directory - pass this to RubyInterpreter.create() as rubyBaseDir */
        val rubyBaseDir: String,

        /** Native libraries directory - pass this to RubyInterpreter.create() as nativeLibsDir */
        val nativeLibsDir: String
    )

    /**
     * Get the default paths for the extracted Ruby VM runtime.
     *
     * This function ensures that the native libraries are loaded and extracted,
     * then returns the correct paths to pass to RubyInterpreter.create().
     *
     * @return Paths object containing installDir, rubyBaseDir, and nativeLibsDir
     */
    fun getDefaultPaths(): Paths {
        // Ensure native libraries are loaded (this happens automatically via RubyVMNative.init,
        // but we make it explicit here)
        NativeLibraryLoader.loadLibrary()

        // Get the installation directory
        val installDir = NativeLibraryLoader.getExtractedLibsDirectory()

        // Detect platform to find native libs directory
        val platformString = detectPlatformString()
        val nativeLibsDir = File(installDir, "native-libs/$platformString")

        return Paths(
            installDir = installDir.absolutePath,
            rubyBaseDir = installDir.absolutePath,  // Fixed: base directory, not /ruby/3.1.0
            nativeLibsDir = nativeLibsDir.absolutePath
        )
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
}
