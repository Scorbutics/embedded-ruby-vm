package com.scorbutics.rubyvm

/**
 * JNI native method declarations for JVM Desktop.
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

    init {
        // Load native library for desktop JVM
        // First tries to load from embedded JAR resources,
        // then falls back to java.library.path
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
