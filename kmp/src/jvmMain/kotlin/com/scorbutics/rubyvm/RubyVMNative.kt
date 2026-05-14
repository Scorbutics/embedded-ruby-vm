package com.scorbutics.rubyvm

/**
 * JNI native method declarations for JVM-based platforms (Android and Desktop).
 * Shared between RubyInterpreter and RubyScript implementations.
 */
internal object RubyVMNative {
    external fun createInterpreter(
        appPath: String,
        rubyBaseDir: String,
        nativeLibsLocation: String,
        listener: JNILogListener
    ): Long

    external fun destroyInterpreter(interpreterPtr: Long)

    external fun createScript(content: String): Long

    external fun destroyScript(scriptPtr: Long)

    external fun enqueueScript(
        interpreterPtr: Long,
        scriptPtr: Long,
        callback: CompletionCallback
    )

    /**
     * Execute a script synchronously (blocking call).
     * This should be called from a JVM thread, not from the main thread.
     *
     * @return 0 on success, non-zero error code on failure
     */
    external fun executeScriptSync(
        interpreterPtr: Long,
        scriptPtr: Long
    ): Int

    external fun enableLogging(interpreterPtr: Long)

    external fun disableLogging(interpreterPtr: Long)

    /**
     * Arm the Ruby `debug` gem's TCP DAP listener so a DAP client (VS Code's
     * rdbg extension, `rdbg --attach`, JetBrains) can attach for live
     * debugging. Eagerly boots the VM; must be called before any
     * `enqueueScript` or `executeScriptSync`.
     *
     * For Android, the recommended deployment is to bind to 127.0.0.1 and
     * use `adb forward tcp:<port> tcp:<port>` on the dev host so the
     * listener never leaves loopback.
     *
     * @param host         Bind address; null means "127.0.0.1"
     * @param port         TCP port, must be > 0
     * @param token        Shared-secret cookie; must be non-empty. Clients
     *                     echo this via the debug gem's RUBY_DEBUG_COOKIE
     *                     handshake.
     * @param sessionName  Optional friendly name surfaced to DAP clients.
     * @return 0 on success; a RubyVMErrorCode (negative) on failure.
     */
    external fun enableRemoteDebug(
        interpreterPtr: Long,
        host: String?,
        port: Int,
        token: String,
        sessionName: String?
    ): Int

    /**
     * Test-only: cumulative count of use-after-free events caught by the
     * JNICallbackContext magic canary in the JNI logging callbacks. A
     * monotonically non-decreasing counter; tests bracket their bodies and
     * assert delta == 0 to detect regressions of the listener-lifetime race
     * fixed by logging_swap_listener.
     */
    external fun getBadMagicCount(): Long

    /**
     * Test-only: synchronously drain the logging system. Blocks until every
     * callback dispatch for data buffered before this call has completed.
     * @param timeoutMs Maximum wait in milliseconds. 0 blocks indefinitely.
     * @return 0 on successful drain, -1 on timeout, -2 if logging not running.
     */
    external fun drainLogging(timeoutMs: Long): Int

    init {
        // Load native library using platform-specific loader
        NativeLibraryLoader.loadLibrary()
    }
}

/**
 * JNI callback interface for log messages
 */
internal interface JNILogListener {
    fun accept(message: String)
    fun onLogError(message: String)
    fun onLogMessage(message: String, source: Int)
}

/**
 * JNI callback interface for script completion
 */
internal interface CompletionCallback {
    fun complete(exitCode: Int)
}
