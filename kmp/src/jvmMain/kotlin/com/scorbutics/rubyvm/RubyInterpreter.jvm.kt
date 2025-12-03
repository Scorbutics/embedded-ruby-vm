package com.scorbutics.rubyvm

import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean

/**
 * JVM implementation of RubyInterpreter using JNI.
 *
 * This implementation wraps the existing JNI layer (RubyVMNative)
 * to provide a Kotlin-friendly API for both Android and Desktop platforms.
 */
actual class RubyInterpreter private constructor(
    private val interpreterPtr: Long,
    private val listener: LogListener
) : Closeable, AutoCloseable {
    private val isDestroyed = AtomicBoolean(false)
    private val destroyLock = Any()

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }

        val callback = object : CompletionCallback {
            override fun complete(exitCode: Int) {
                onComplete(exitCode)
            }
        }

        RubyVMNative.enqueueScript(interpreterPtr, script.scriptPtr, callback)
    }

    actual fun enableLogging() {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }

        RubyVMNative.enableLogging(interpreterPtr)
    }

    actual fun disableLogging() {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }

        RubyVMNative.disableLogging(interpreterPtr)
    }

    actual fun destroy() {
        // Use compareAndSet to atomically check and set the flag
        if (isDestroyed.compareAndSet(false, true)) {
            synchronized(destroyLock) {
                RubyVMNative.destroyInterpreter(interpreterPtr)
            }
        }
    }

    actual override fun close() {
        destroy()
    }

    actual companion object {
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
