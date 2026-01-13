package com.scorbutics.rubyvm

import android.content.Context
import java.io.File

/**
 * Android-specific utility object to get paths for Ruby VM runtime and native libraries.
 *
 * On Android, the Ruby runtime files are extracted to the app's internal storage directory.
 * Native libraries (.so files) are automatically loaded from the APK's lib directory by the system.
 *
 * Use this object to get the correct paths when creating a RubyInterpreter.
 *
 * Example:
 * ```kotlin
 * val paths = RubyVMPaths.getDefaultPaths(context)
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
        /** Base installation directory (e.g., "/data/data/com.example.app/files/ruby-vm") */
        val installDir: String,

        /** Ruby base directory - pass this to RubyInterpreter.create() as rubyBaseDir */
        val rubyBaseDir: String,

        /** Native libraries directory - pass this to RubyInterpreter.create() as nativeLibsDir */
        val nativeLibsDir: String
    )

    /**
     * Get the default paths for the Ruby VM runtime on Android.
     *
     * On Android, the Ruby runtime files are extracted to the app's internal storage.
     * Native libraries (.so files) are packaged in the APK and loaded automatically by Android.
     *
     * @param context Android Context (typically Activity or Application context)
     * @return Paths object containing installDir, rubyBaseDir, and nativeLibsDir
     */
    fun getDefaultPaths(context: Context): Paths {
        // Ensure native libraries are loaded
        NativeLibraryLoader.loadLibrary()

        // Use app's internal files directory for Ruby runtime
        val installDir = File(context.filesDir, "ruby-vm")
        installDir.mkdirs()

        // Detect platform for native libs directory structure
        val platformString = detectPlatformString()
        val nativeLibsDir = File(installDir, "native-libs/$platformString")

        return Paths(
            installDir = installDir.absolutePath,
            rubyBaseDir = installDir.absolutePath,
            nativeLibsDir = nativeLibsDir.absolutePath
        )
    }

    /**
     * Detect platform string for Android (e.g., "aarch64-android" or "x86_64-android")
     */
    private fun detectPlatformString(): String {
        val arch = System.getProperty("os.arch")?.lowercase() ?: "unknown"

        val archName = when {
            arch.contains("aarch64") || arch.contains("arm64") -> "aarch64"
            arch.contains("amd64") || arch.contains("x86_64") -> "x86_64"
            arch.contains("arm") -> "arm"
            arch.contains("x86") -> "x86"
            else -> arch
        }

        return "$archName-android"
    }
}
