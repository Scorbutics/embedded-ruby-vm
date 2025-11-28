package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*
import platform.posix.pthread_self

/**
 * Native (iOS/macOS/Linux) implementation of RubyInterpreter using cinterop.
 *
 * This implementation directly calls C functions through Kotlin/Native's cinterop,
 * avoiding the JNI layer entirely for better performance on native platforms.
 */
@OptIn(ExperimentalForeignApi::class)
actual class RubyInterpreter private constructor(
    private val interpreterPtr: CPointer<ruby_interpreter>?,
    private val listener: LogListener,
    private val stableRefHolder: StableRefHolder
) {
    private var isDestroyed = false

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        require(script.scriptPtr != null) { "Script has been destroyed" }

        // Create stable reference for the callback
        val callbackRef = StableRef.create(onComplete)

        // Create completion task with callback
        val completionTask = nativeHeap.alloc<completion_task>().apply {
            this.callback = staticCFunction { exitCode, userData ->
                val callback = userData?.asStableRef<(Int) -> Unit>()?.get()
                callback?.invoke(exitCode)
                // Dispose the stable reference
                userData?.asStableRef<(Int) -> Unit>()?.dispose()
            }
            this.user_data = callbackRef.asCPointer()
        }

        ruby_interpreter_enqueue(interpreterPtr, script.scriptPtr, completionTask.ptr)

        nativeHeap.free(completionTask)
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
            listener: LogListener
        ): RubyInterpreter {
            // Create stable references for the listener callbacks
            val onLogRef = StableRef.create { message: String ->
                listener.onLog(message)
            }

            val onErrorRef = StableRef.create { message: String ->
                listener.onError(message)
            }

            val holder = StableRefHolder(onLogRef, onErrorRef)

            // Create log listener structure
            val logListener = nativeHeap.alloc<log_listener>().apply {
                this.on_log = staticCFunction { message, userData ->
                    val callback = userData?.asStableRef<(String) -> Unit>()?.get()
                    callback?.invoke(message?.toKString() ?: "")
                }
                this.on_log_error = staticCFunction { message, userData ->
                    val callback = userData?.asStableRef<(String) -> Unit>()?.get()
                    callback?.invoke(message?.toKString() ?: "")
                }
                this.on_log_user_data = onLogRef.asCPointer()
                this.on_log_error_user_data = onErrorRef.asCPointer()
            }

            val interpreterPtr = ruby_interpreter_create(
                appPath,
                rubyBaseDir,
                nativeLibsDir,
                logListener.ptr
            )

            nativeHeap.free(logListener)

            require(interpreterPtr != null) { "Failed to create Ruby interpreter" }

            return RubyInterpreter(interpreterPtr, listener, holder)
        }
    }
}

/**
 * Helper class to hold stable references and dispose them together
 */
@OptIn(ExperimentalForeignApi::class)
private class StableRefHolder(
    private val onLogRef: StableRef<(String) -> Unit>,
    private val onErrorRef: StableRef<(String) -> Unit>
) {
    fun dispose() {
        onLogRef.dispose()
        onErrorRef.dispose()
    }
}
