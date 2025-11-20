package com.scorbutics.rubyvm

/**
 * Android implementation of RubyInterpreter using JNI.
 *
 * This implementation wraps the existing JNI layer (RubyVMNative)
 * to provide a Kotlin-friendly API.
 */
actual class RubyInterpreter private constructor(
    private val interpreterPtr: Long,
    private val listener: LogListener
) {
    private var isDestroyed = false

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        val callback = object : CompletionCallback {
            override fun complete(exitCode: Int) {
                onComplete(exitCode)
            }
        }

        RubyVMNative.enqueueScript(interpreterPtr, script.scriptPtr, callback)
    }

    actual fun destroy() {
        if (!isDestroyed) {
            RubyVMNative.destroyInterpreter(interpreterPtr)
            isDestroyed = true
        }
    }

    actual companion object {
        init {
            // Load native library
            System.loadLibrary("embedded-ruby")
        }

        actual fun create(
            appPath: String,
            rubyBaseDir: String,
            nativeLibsDir: String,
            listener: LogListener
        ): RubyInterpreter {
            val jniListener = object : JNILogListener {
                override fun accept(message: String) {
                    listener.onLog(message)
                }

                override fun onLogError(message: String) {
                    listener.onError(message)
                }
            }

            val interpreterPtr = RubyVMNative.createInterpreter(
                appPath,
                rubyBaseDir,
                nativeLibsDir,
                jniListener
            )

            require(interpreterPtr != 0L) { "Failed to create Ruby interpreter" }

            return RubyInterpreter(interpreterPtr, listener)
        }
    }
}

/**
 * JNI callback interface for log messages
 */
private interface JNILogListener {
    fun accept(message: String)
    fun onLogError(message: String)
}

/**
 * JNI callback interface for script completion
 */
private interface CompletionCallback {
    fun complete(exitCode: Int)
}

/**
 * Native method declarations (implemented in JNI layer)
 */
private object RubyVMNative {
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
}
