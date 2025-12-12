package com.scorbutics.rubyvm

/**
 * JNI native method declarations for JVM-based platforms (Android and Desktop).
 * Shared between RubyInterpreter and RubyScript implementations.
 */
internal object RubyVMNative {
    external fun createInterpreter(
        appPath: String,
        rubyBaseDir: String,
        nativeLibsLocation: String,
        listener: JNILogListener
    ): Long

    external fun destroyInterpreter(interpreterPtr: Long)

    external fun createScript(content: String): Long

    external fun destroyScript(scriptPtr: Long)

    external fun enqueueScript(
        interpreterPtr: Long,
        scriptPtr: Long,
        callback: CompletionCallback
    )

    /**
     * Execute a script synchronously (blocking call).
     * This should be called from a JVM thread, not from the main thread.
     *
     * @return 0 on success, non-zero error code on failure
     */
    external fun executeScriptSync(
        interpreterPtr: Long,
        scriptPtr: Long
    ): Int

    external fun enableLogging(interpreterPtr: Long)

    external fun disableLogging(interpreterPtr: Long)

    // ========================================================================
    // Asset Layout Query (Phase 2)
    // ========================================================================

    /**
     * Get the native library directory path from the C-managed asset layout.
     *
     * @param installDir The installation directory
     * @return Path to the native libs directory, or null on error
     */
    external fun getNativeLibsPath(installDir: String): String?

    /**
     * Get the count of embedded native libraries.
     *
     * @param installDir The installation directory
     * @return Number of native libraries, or -1 on error
     */
    external fun getNativeLibCount(installDir: String): Int

    /**
     * Get a specific native library full path by index.
     *
     * @param installDir The installation directory
     * @param index The library index (0 to count-1)
     * @return Full path to the library file, or null if invalid
     */
    external fun getNativeLibPath(installDir: String, index: Int): String?

    init {
        // Load native library using platform-specific loader
        NativeLibraryLoader.loadLibrary()
    }
}

/**
 * JNI callback interface for log messages
 */
internal interface JNILogListener {
    fun accept(message: String)
    fun onLogError(message: String)
}

/**
 * JNI callback interface for script completion
 */
internal interface CompletionCallback {
    fun complete(exitCode: Int)
}
