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
     * On Android, the Ruby runtime files are extracted to the app's internal storage
     * by the NativeLibraryLoader during initialization.
     *
     * @param context Android Context (typically Activity or Application context)
     * @return Paths object containing installDir, rubyBaseDir, and nativeLibsDir
     */
    fun getDefaultPaths(context: Context): Paths {
        // Set context and ensure native libraries are loaded
        NativeLibraryLoader.setContext(context)
        NativeLibraryLoader.loadLibrary()

        // Get the directory where files were extracted
        val installDir = NativeLibraryLoader.getExtractedLibsDirectory()

        // Detect platform for native libs directory structure
        val platformString = NativeLibraryLoaderHelper.detectPlatformString()
        val nativeLibsDir = File(installDir, "native-libs/$platformString")

        return Paths(
            installDir = installDir.absolutePath,
            rubyBaseDir = installDir.absolutePath,
            nativeLibsDir = nativeLibsDir.absolutePath
        )
    }
}
