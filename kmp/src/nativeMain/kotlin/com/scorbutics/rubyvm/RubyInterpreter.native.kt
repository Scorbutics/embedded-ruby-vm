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
                // IMPORTANT: All callback fields must be explicitly zeroed.
                // If any callback is non-null, ruby_vm_start() auto-enables the
                // logging system which spawns a C pthread. That thread invokes the
                // callback, but Kotlin methods called from a raw C thread that isn't
                // attached to the Kotlin/Native runtime cause a SIGSEGV.
                //
                // Setting all callbacks to null disables the logging redirect entirely.
                // Ruby output goes to normal stdout/stderr instead.
                // TODO: implement thread-safe logging using a lock-free queue that
                //       the Kotlin main thread drains, instead of direct callbacks.
                logListener.context = null
                logListener.user_data = null
                logListener.accept = null
                logListener.on_log_error = null
                logListener.on_log_message = null

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
