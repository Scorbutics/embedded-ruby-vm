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

    actual fun enableRemoteDebug(host: String?, port: Int, token: String, sessionName: String?): Int {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        return memScoped {
            val opts = alloc<RubyVMRemoteDebugOptions>()
            opts.host = host?.cstr?.getPointer(this)
            opts.port = port
            opts.token = token.cstr.getPointer(this)
            opts.session_name = sessionName?.cstr?.getPointer(this)
            ruby_interpreter_enable_remote_debug(interpreterPtr?.reinterpret(), opts.ptr)
        }
    }

    actual fun enableRemoteEval(host: String?, port: Int, token: String, sessionName: String?): Int {
        check(!isDestroyed) { "Interpreter has been destroyed" }
        return memScoped {
            val opts = alloc<RubyVMRemoteEvalOptions>()
            opts.host = host?.cstr?.getPointer(this)
            opts.port = port
            opts.token = token.cstr.getPointer(this)
            opts.session_name = sessionName?.cstr?.getPointer(this)
            ruby_interpreter_enable_remote_eval(interpreterPtr?.reinterpret(), opts.ptr)
        }
    }

    actual fun destroy() {
        if (!isDestroyed && interpreterPtr != null) {
            // Disable logging BEFORE destroying the interpreter and disposing the
            // StableRef. The logging thread runs independently and would otherwise
            // call the Kotlin callback via the StableRef after it has been disposed,
            // causing a use-after-free crash.
            ruby_interpreter_disable_logging(interpreterPtr.reinterpret())

            // Call C function directly (works for static builds)
            ruby_interpreter_destroy(interpreterPtr.reinterpret())
            isDestroyed = true

            // Dispose stable references — now safe because the logging thread
            // can no longer invoke the callback
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
                // The logging system spawns a C pthread that invokes the callback.
                // With Kotlin/Native's new memory model (1.7.20+), StableRef and
                // Kotlin object access from foreign threads is supported.
                logListener.context = listenerRef.asCPointer()
                logListener.user_data = null
                logListener.on_log_message = staticCFunction { listenerPtr, message, source, level ->
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
                        else -> LogSource.NATIVE_STDERR // fallback for unknown source
                    }
                    val logLevel = when (level.toInt()) {
                        1 -> LogLevel.DEBUG
                        2 -> LogLevel.INFO
                        3 -> LogLevel.ERROR
                        else -> LogLevel.INFO // matches derive_default_level_for_stream
                    }
                    ktListener.onLogMessage(LogMessage(message.toKString(), logSource, logLevel))
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
