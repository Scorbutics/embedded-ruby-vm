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

    external fun enableLogging(interpreterPtr: Long)

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
