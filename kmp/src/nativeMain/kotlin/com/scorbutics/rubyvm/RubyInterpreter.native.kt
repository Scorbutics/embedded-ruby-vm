package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*
import kotlin.native.Platform
import kotlin.experimental.ExperimentalNativeApi

/**
 * Native (iOS/macOS/Linux) implementation of RubyInterpreter using cinterop.
 *
 * This implementation uses dynamic library loading (dlopen) to load the Ruby VM at runtime.
 * The library is extracted by libassets.a and then loaded via ruby_api_load().
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalNativeApi::class)
actual class RubyInterpreter private constructor(
    private val api: RubyAPI,
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

        // Create completion task with callback
        memScoped {
            val completionTask = alloc<RubyCompletionTask>().apply {
                this.callback = staticCFunction { userData, exitCode ->
                    val callback = userData?.asStableRef<(Int) -> Unit>()?.get()
                    callback?.invoke(exitCode)
                    // Dispose the stable reference
                    userData?.asStableRef<(Int) -> Unit>()?.dispose()
                }
                this.user_data = callbackRef.asCPointer()
            }

            // Use the API function pointer from dynamically loaded library
            val enqueueFunc = api.interpreter.enqueue
            requireNotNull(enqueueFunc) { "enqueue function not loaded from API" }

            enqueueFunc(interpreterPtr?.reinterpret(), script.scriptPtr?.reinterpret(), completionTask.readValue())
        }
    }

    actual fun enableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        val enableLoggingFunc = api.interpreter.enable_logging
        requireNotNull(enableLoggingFunc) { "enable_logging function not loaded from API" }

        enableLoggingFunc(interpreterPtr?.reinterpret())
    }

    actual fun disableLogging() {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        val disableLoggingFunc = api.interpreter.disable_logging
        requireNotNull(disableLoggingFunc) { "disable_logging function not loaded from API" }

        disableLoggingFunc(interpreterPtr?.reinterpret())
    }

    actual fun destroy() {
        if (!isDestroyed && interpreterPtr != null) {
            val destroyFunc = api.interpreter.destroy
            requireNotNull(destroyFunc) { "destroy function not loaded from API" }

            destroyFunc(interpreterPtr.reinterpret())
            isDestroyed = true

            // Dispose stable references
            stableRefHolder.dispose()
        }
    }

    actual override fun close() {
        destroy()
    }

    actual companion object {
        // Global API instance (loaded once, reused for all interpreters)
        private var loadedAPI: RubyAPI? = null

        /**
         * Load the Ruby API dynamically using dlopen/dlsym.
         * This is called once and cached for all interpreter instances.
         */
        private fun ensureAPILoaded(nativeLibsDir: String): RubyAPI {
            if (loadedAPI == null) {
                // Determine library extension based on platform
                val libExtension = when {
                    Platform.osFamily == OsFamily.MACOSX -> "dylib"
                    Platform.osFamily == OsFamily.LINUX -> "so"
                    else -> error("Unsupported platform: ${Platform.osFamily}")
                }

                val libPath = "$nativeLibsDir/libembedded-ruby.$libExtension"

                memScoped {
                    // Allocate API structure
                    val api = alloc<RubyAPI>()
                    // Load the library and populate function pointers
                    val result = ruby_api_load(libPath, api.ptr)
                    require(result == 0) {
                        "Failed to load Ruby API from $libPath. Make sure libassets has extracted the runtime first."
                    }

                    // Cache the loaded API (copy value, not pointer)
                    loadedAPI = api.ptr.pointed

                    // Also store in global holder for RubyScript access
                    RubyAPIHolder.setAPI(loadedAPI!!)
                }
            }
            return loadedAPI!!
        }

        actual fun create(
            appPath: String,
            rubyBaseDir: String,
            nativeLibsDir: String,
            listener: com.scorbutics.rubyvm.LogListener
        ): RubyInterpreter {
            // First, ensure the API is loaded dynamically
            val api = ensureAPILoaded(nativeLibsDir)

            // Create stable reference for the listener itself
            val listenerRef = StableRef.create(listener)
            val holder = StableRefHolder(listenerRef, listenerRef) // Same ref for both

            memScoped {
                // Create log listener structure
                val logListener = alloc<LogListener>().apply {
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

                // Use API function pointer to create interpreter
                val createFunc = api.interpreter.create
                requireNotNull(createFunc) { "create function not loaded from API" }

                val interpreterPtr = createFunc(
                    appPath.cstr.ptr,
                    rubyBaseDir.cstr.ptr,
                    nativeLibsDir.cstr.ptr,
                    logListener.readValue()
                )

                require(interpreterPtr != null) { "Failed to create Ruby interpreter" }

                return RubyInterpreter(api, interpreterPtr, listener, holder)
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
