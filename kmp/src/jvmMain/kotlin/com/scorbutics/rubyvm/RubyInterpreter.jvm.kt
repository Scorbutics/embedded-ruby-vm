package com.scorbutics.rubyvm

import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * JVM implementation of RubyInterpreter using JNI.
 *
 * This implementation wraps the existing JNI layer (RubyVMNative)
 * to provide a Kotlin-friendly API for both Android and Desktop platforms.
 *
 * IMPORTANT: Uses synchronous execution on JVM threads to avoid native pthreads
 * from attaching to the JVM, which would cause JVM GC to crash.
 */
@OptIn(ExperimentalStdlibApi::class)
actual class RubyInterpreter private constructor(
    private val interpreterPtr: Long,
    private val listener: LogListener
) : Closeable, AutoCloseable {
    private val isDestroyed = AtomicBoolean(false)
    private val destroyLock = Any()

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }

        // Spawn a JVM thread (NOT a native pthread!) to execute the script
        // This thread will block in native code, but since it's a JVM thread,
        // the JVM GC only scans its JVM stack, not native pthread stacks
        thread(name = "RubyVM-ScriptExecution", isDaemon = false) {
            try {
                // Call the synchronous native method - this BLOCKS until script completes
                val exitCode = RubyVMNative.executeScriptSync(interpreterPtr, script.scriptPtr)
                onComplete(exitCode)
            } catch (e: Exception) {
                System.err.println("[RubyVM] Error executing script: ${e.message}")
                e.printStackTrace()
                onComplete(1)  // Error
            }
        }
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

    @OptIn(ExperimentalStdlibApi::class)
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
