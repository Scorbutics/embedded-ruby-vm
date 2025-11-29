package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*
import platform.posix.pthread_self

// Type aliases to avoid naming conflicts between Kotlin classes and C structs
@OptIn(ExperimentalForeignApi::class)
internal typealias CRubyInterpreter = com.scorbutics.rubyvm.native.RubyInterpreter

@OptIn(ExperimentalForeignApi::class)
internal typealias CLogListener = com.scorbutics.rubyvm.native.LogListener

@OptIn(ExperimentalForeignApi::class)
internal typealias CRubyCompletionTask = com.scorbutics.rubyvm.native.RubyCompletionTask

/**
 * Native (iOS/macOS/Linux) implementation of RubyInterpreter using cinterop.
 *
 * This implementation directly calls C functions through Kotlin/Native's cinterop,
 * avoiding the JNI layer entirely for better performance on native platforms.
 */
@OptIn(ExperimentalForeignApi::class)
actual class RubyInterpreter private constructor(
    private val interpreterPtr: CPointer<CRubyInterpreter>?,
    private val listener: com.scorbutics.rubyvm.LogListener,
    private val stableRefHolder: StableRefHolder
) {
    private var isDestroyed = false

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        require(script.scriptPtr != null) { "Script has been destroyed" }

        // Create stable reference for the callback
        val callbackRef = StableRef.create(onComplete)

        // Create completion task with callback
        val completionTask = nativeHeap.alloc<CRubyCompletionTask>().apply {
            this.callback = staticCFunction { userData, exitCode ->
                val callback = userData?.asStableRef<(Int) -> Unit>()?.get()
                callback?.invoke(exitCode)
                // Dispose the stable reference
                userData?.asStableRef<(Int) -> Unit>()?.dispose()
            }
            this.user_data = callbackRef.asCPointer()
        }

        ruby_interpreter_enqueue(interpreterPtr, script.scriptPtr?.reinterpret(), completionTask.readValue())

        nativeHeap.free(completionTask)
    }

    actual fun enableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        ruby_interpreter_enable_logging(interpreterPtr)
    }

    actual fun disableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        ruby_interpreter_disable_logging(interpreterPtr)
    }

    actual fun destroy() {
        if (!isDestroyed && interpreterPtr != null) {
            ruby_interpreter_destroy(interpreterPtr)
            isDestroyed = true

            // Dispose stable references
            stableRefHolder.dispose()
        }
    }

    actual companion object {
        actual fun create(
            appPath: String,
            rubyBaseDir: String,
            nativeLibsDir: String,
            listener: com.scorbutics.rubyvm.LogListener
        ): RubyInterpreter {
            // Create stable reference for the listener itself
            val listenerRef = StableRef.create(listener)
            val holder = StableRefHolder(listenerRef, listenerRef) // Same ref for both

            // Create log listener structure
            val logListener = nativeHeap.alloc<CLogListener>().apply {
                // Store the listener reference in context
                this.context = listenerRef.asCPointer()
                this.user_data = null

                // Set callback function pointers
                this.accept = staticCFunction { listenerPtr, message ->
                    val listener = listenerPtr?.pointed?.context
                        ?.asStableRef<com.scorbutics.rubyvm.LogListener>()?.get()
                    listener?.onLog(message?.toKString() ?: "")
                }
                this.on_log_error = staticCFunction { listenerPtr, message ->
                    val listener = listenerPtr?.pointed?.context
                        ?.asStableRef<com.scorbutics.rubyvm.LogListener>()?.get()
                    listener?.onError(message?.toKString() ?: "")
                }
            }

            val interpreterPtr = ruby_interpreter_create(
                appPath,
                rubyBaseDir,
                nativeLibsDir,
                logListener.readValue()
            )

            nativeHeap.free(logListener)

            require(interpreterPtr != null) { "Failed to create Ruby interpreter" }

            return RubyInterpreter(interpreterPtr?.reinterpret(), listener, holder)
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
