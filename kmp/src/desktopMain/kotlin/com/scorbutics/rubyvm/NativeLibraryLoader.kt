package com.scorbutics.rubyvm

import java.io.File

/**
 * Desktop implementation of native library loader.
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
     * Load the native libraries for the current platform.
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
}
