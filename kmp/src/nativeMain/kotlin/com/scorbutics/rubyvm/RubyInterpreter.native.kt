package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*
import kotlin.native.Platform
import kotlin.experimental.ExperimentalNativeApi

/**
 * Native (iOS/macOS/Linux) implementation of RubyInterpreter using cinterop.
 *
 * For static builds: Calls C functions directly (statically linked).
 * For dynamic builds: Would use dlopen/dlsym (future enhancement).
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalNativeApi::class, ExperimentalStdlibApi::class)
actual class RubyInterpreter private constructor(
    private val interpreterPtr: COpaquePointer?,
    private val listener: com.scorbutics.rubyvm.LogListener,
    private val stableRefHolder: StableRefHolder
) : AutoCloseable {
    private var isDestroyed = false

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        require(script.scriptPtr != null) { "Script has been destroyed" }

        // Create stable reference for the callback
        val callbackRef = StableRef.create(onComplete)

        // Create completion task
        memScoped {
            val completionTask = alloc<RubyCompletionTask>()
            completionTask.callback = staticCFunction { userData, exitCode ->
                val callback = userData?.asStableRef<(Int) -> Unit>()?.get()
                callback?.invoke(exitCode)
                // Dispose the stable reference
                userData?.asStableRef<(Int) -> Unit>()?.dispose()
            }
            completionTask.user_data = callbackRef.asCPointer()

            // Call C function directly (works for static builds)
            ruby_interpreter_enqueue(
                interpreterPtr?.reinterpret(),
                script.scriptPtr?.reinterpret(),
                completionTask.readValue()
            )
        }
    }

    actual fun enableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        // Call C function directly (works for static builds)
        ruby_interpreter_enable_logging(interpreterPtr?.reinterpret())
    }

    actual fun disableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        // Call C function directly (works for static builds)
        ruby_interpreter_disable_logging(interpreterPtr?.reinterpret())
    }

    actual fun destroy() {
        if (!isDestroyed && interpreterPtr != null) {
            // Call C function directly (works for static builds)
            ruby_interpreter_destroy(interpreterPtr.reinterpret())
            isDestroyed = true

            // Dispose stable references
            stableRefHolder.dispose()
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
            listener: com.scorbutics.rubyvm.LogListener
        ): RubyInterpreter {
            // Create stable reference for the listener
            val listenerRef = StableRef.create(listener)
            val holder = StableRefHolder(listenerRef, listenerRef) // Same ref for both

            // Create log listener structure
            memScoped {
                val logListener = alloc<com.scorbutics.rubyvm.native.LogListener>()
                // IMPORTANT: All callback fields must be explicitly initialized.
                // The logging system spawns a C pthread that invokes these callbacks.
                // With Kotlin/Native's new memory model (1.7.20+), StableRef and
                // Kotlin object access from foreign threads is supported.
                // We use on_log_message (preferred by the C code) and set legacy
                // callbacks to null to avoid ambiguity.
                logListener.context = listenerRef.asCPointer()
                logListener.user_data = null
                logListener.accept = null
                logListener.on_log_error = null
                logListener.on_log_message = staticCFunction { listenerPtr, message, source ->
                    if (listenerPtr == null || message == null) return@staticCFunction
                    val ktListener = listenerPtr.pointed.context
                        ?.asStableRef<com.scorbutics.rubyvm.LogListener>()
                        ?.get() ?: return@staticCFunction
                    val logSource = when (source.toInt()) {
                        1 -> LogSource.RUBY_STDOUT
                        2 -> LogSource.RUBY_STDERR
                        3 -> LogSource.VMLOGGER
                        4 -> LogSource.NATIVE_STDOUT
                        5 -> LogSource.NATIVE_STDERR
                        else -> LogSource.NATIVE_STDOUT
                    }
                    ktListener.onLogMessage(LogMessage(message.toKString(), logSource))
                }

                // Call C function directly (works for static builds)
                val interpreterPtr = ruby_interpreter_create(
                    appPath,
                    rubyBaseDir,
                    nativeLibsDir,
                    logListener.readValue()
                )

                require(interpreterPtr != null) { "Failed to create Ruby interpreter" }

                // Mark as initialized
                RubyAPIHolder.setInitialized()

                return RubyInterpreter(interpreterPtr, listener, holder)
            }
        }
    }
}

/**
 * Helper class to hold stable references and dispose them together
 */
@OptIn(ExperimentalForeignApi::class)
private class StableRefHolder(
    private val listenerRef: StableRef<com.scorbutics.rubyvm.LogListener>,
    @Suppress("UNUSED_PARAMETER") private val unused: StableRef<*>? = null
) {
    fun dispose() {
        listenerRef.dispose()
    }
}
