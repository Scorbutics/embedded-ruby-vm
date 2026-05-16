package com.scorbutics.rubyvm

import java.io.Closeable
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
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

    actual fun enableRemoteDebug(host: String?, port: Int, token: String, sessionName: String?): Int {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }
        return runOnWorker("RubyVM-EnableRemoteDebug") {
            RubyVMNative.enableRemoteDebug(interpreterPtr, host, port, token, sessionName)
        }
    }

    actual fun enableRemoteEval(host: String?, port: Int, token: String, sessionName: String?): Int {
        check(!isDestroyed.get()) { "Interpreter has been destroyed" }
        return runOnWorker("RubyVM-EnableRemoteEval") {
            RubyVMNative.enableRemoteEval(interpreterPtr, host, port, token, sessionName)
        }
    }

    /**
     * Run [block] on a fresh JVM worker thread and block until it returns
     * its int result. Both enable_remote_* JNI calls eagerly boot the VM,
     * which spawns a native pthread for the FIFO interpreter; if Ruby's
     * GC signals reached the JVM main thread later, the JVM would crash.
     * Same constraint that drives `enqueue`'s thread{} wrapper.
     */
    private fun runOnWorker(name: String, block: () -> Int): Int {
        val result = AtomicInteger(0)
        val latch = CountDownLatch(1)
        thread(name = name, isDaemon = false) {
            try {
                result.set(block())
            } catch (e: Exception) {
                System.err.println("[RubyVM] $name failed: ${e.message}")
                e.printStackTrace()
                result.set(-1)
            } finally {
                latch.countDown()
            }
        }
        latch.await()
        return result.get()
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
                override fun onLogMessage(message: String, source: Int, level: Int) {
                    val logSource = when (source) {
                        1 -> LogSource.RUBY_STDOUT
                        2 -> LogSource.RUBY_STDERR
                        3 -> LogSource.VMLOGGER
                        4 -> LogSource.NATIVE_STDOUT
                        5 -> LogSource.NATIVE_STDERR
                        else -> LogSource.NATIVE_STDERR // fallback
                    }
                    val logLevel = when (level) {
                        1 -> LogLevel.DEBUG
                        2 -> LogLevel.INFO
                        3 -> LogLevel.ERROR
                        else -> LogLevel.INFO // matches derive_default_level_for_stream
                    }
                    listener.onLogMessage(LogMessage(message, logSource, logLevel))
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
