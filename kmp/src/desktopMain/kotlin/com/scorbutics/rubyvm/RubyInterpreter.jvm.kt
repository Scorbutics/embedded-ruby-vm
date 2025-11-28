package com.scorbutics.rubyvm

/**
 * JVM Desktop implementation of RubyInterpreter using JNI.
 *
 * This is essentially the same as Android implementation,
 * but with different native library loading strategy.
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
            // Load native library for desktop JVM
            // Library should be in java.library.path
            try {
                System.loadLibrary("embedded-ruby")
            } catch (e: UnsatisfiedLinkError) {
                throw RuntimeException("Failed to load native library. Make sure libembedded-ruby is in java.library.path", e)
            }
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
