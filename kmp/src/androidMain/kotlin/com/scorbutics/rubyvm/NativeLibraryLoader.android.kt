package com.scorbutics.rubyvm

import android.content.Context
import java.io.File

/**
 * Android implementation of native library loader.
 * Implements two-phase loading to solve circular dependency:
 *
 * Phase 1: Load libassets.so (no dependencies)
 *          Extract embedded native libraries (libruby.so, etc.)
 *          Load extracted libruby.so
 *
 * Phase 2: Load the configured library (depends on libruby.so)
 *
 * This solves the bootstrap problem where the main library requires libruby.so,
 * but libruby.so is embedded in libassets.so.
 */
internal actual object NativeLibraryLoader {
    private var loaded = false
    private var cachedInstallDir: File? = null
    private var isStaticBuild = false

    // Android context must be set before calling loadLibrary
    private var applicationContext: Context? = null

    /**
     * Set the Android application context.
     * This must be called before loadLibrary() to determine the install directory.
     *
     * @param context Android Application or Activity context
     */
    fun setContext(context: Context) {
        applicationContext = context.applicationContext
    }

    /**
     * Get the installation directory where native libraries are extracted.
     * This must be called AFTER loadLibrary() has been called at least once.
     *
     * @return The directory containing extracted native libraries and Ruby runtime
     */
    fun getExtractedLibsDirectory(): File {
        return cachedInstallDir ?: throw IllegalStateException(
            "Native libraries not yet loaded. Call RubyVMNative initialization first " +
            "(happens automatically when you import RubyVMNative)."
        )
    }

    actual fun isLoaded(): Boolean = loaded

    /**
     * Load the native libraries for the Android platform.
     * Uses two-phase loading to handle circular dependencies.
     */
    @Synchronized
    actual fun loadLibrary() {
        if (loaded) {
            return
        }

        val libraryName = LibraryConfig.libraryName

        // PHASE 1: Load assets library and extract native dependencies
        val installDir = NativeLibraryLoaderHelper.performPhase1Loading {
            getInstallDirectory()
        }
        cachedInstallDir = installDir

        // PHASE 2: Load main embedded-ruby library
        isStaticBuild = NativeLibraryLoaderHelper.performPhase2Loading(libraryName)
        loaded = true

        println("=== Native library loading complete ===")
    }

    /**
     * Get the installation directory for extracted native libraries on Android.
     * Uses the app's internal files directory.
     */
    private fun getInstallDirectory(): File {
        val context = applicationContext
            ?: throw IllegalStateException(
                "Android context not set. Call NativeLibraryLoader.setContext() before loading libraries."
            )

        // Inform the native layer about Android's app data directory
        // This allows the C code to automatically use the correct path
        // The C code will append "/ruby-vm" to this path automatically
        try {
            AssetsNative.setAndroidAppDataDir(context.filesDir.absolutePath)
            println("✓ Configured Android app data directory: ${context.filesDir.absolutePath}")
        } catch (e: Exception) {
            println("Warning: Failed to set Android app data directory: ${e.message}")
        }

        // Try to use the default from C code first (which now knows about Android)
        val defaultDir = try {
            AssetsNative.getDefaultInstallDir()
        } catch (e: Exception) {
            null
        }

        if (defaultDir != null) {
            val dir = File(defaultDir)
            dir.mkdirs()
            println("✓ Using install directory: ${dir.absolutePath}")
            return dir
        }

        // Fallback to app's internal files directory
        // This should rarely happen now that we've set the Android app data dir
        val installDir = File(context.filesDir, "ruby-vm")
        installDir.mkdirs()
        println("✓ Using fallback install directory: ${installDir.absolutePath}")
        return installDir
    }
}
