package com.scorbutics.rubyvm

/**
 * Ruby interpreter that can execute Ruby scripts.
 *
 * The interpreter manages a Ruby VM instance and provides methods to
 * execute scripts asynchronously with callbacks.
 *
 * **Important: Singleton VM constraint.** The underlying Ruby runtime only supports
 * a single VM per process. Creating multiple [RubyInterpreter] instances is allowed
 * but they all share the same underlying VM. The first interpreter's paths
 * (appPath, rubyBaseDir, nativeLibsDir) are used to initialize the VM; subsequent
 * interpreters reuse it and only their log listener is applied. This is a Ruby
 * limitation, not a design choice — treat [RubyInterpreter] as process-global.
 *
 * Platform implementations:
 * - Android/JVM: Uses JNI to call native C library
 * - iOS/macOS/Linux: Uses Kotlin/Native cinterop to call C directly
 *
 * Example usage:
 * ```kotlin
 * val listener = object : LogListener {
 *     override fun onLog(message: String) = println("[Ruby] $message")
 *     override fun onError(message: String) = println("[Error] $message")
 * }
 *
 * val interpreter = RubyInterpreter.create(
 *     appPath = "/path/to/app",
 *     rubyBaseDir = "/path/to/ruby/stdlib",
 *     nativeLibsDir = "/path/to/native/libs",
 *     listener = listener
 * )
 *
 * val script = RubyScript.fromContent("puts 'Hello from Ruby!'")
 * interpreter.enqueue(script) { exitCode ->
 *     println("Script finished with exit code: $exitCode")
 *     script.destroy()
 * }
 *
 * // When done with interpreter
 * interpreter.destroy()
 * ```
 */
@OptIn(ExperimentalStdlibApi::class)
expect class RubyInterpreter : AutoCloseable {
    /**
     * Enqueue a script for execution on the Ruby VM.
     *
     * Scripts are executed sequentially in the order they are enqueued.
     * The completion callback is invoked when the script finishes.
     *
     * @param script The script to execute
     * @param onComplete Callback invoked with the script's exit code (0 = success)
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit)

    /**
     * Enable logging output from the Ruby VM.
     *
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun enableLogging()

    /**
     * Disable logging output from the Ruby VM.
     *
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun disableLogging()

    /**
     * Arm the Ruby `debug` gem's DAP listener so a DAP client (VS Code's
     * rdbg extension, `rdbg --attach`, JetBrains) can attach for live
     * debugging — breakpoints, step in/over/out, locals, exceptions.
     *
     * Eagerly boots the VM with the listener armed in non-stop mode, so the
     * port is available before the first script is enqueued. Must be called
     * before any [enqueue] / `executeScriptSync` (otherwise returns
     * [RubyVMErrorCode.ALREADY_STARTED]).
     *
     * For Android: bind to "127.0.0.1" (default) and tunnel via
     * `adb forward tcp:<port> tcp:<port>` from the dev host.
     *
     * @param host        Bind address; null means "127.0.0.1".
     * @param port        TCP port, must be > 0.
     * @param token       Shared-secret cookie; must be non-empty. Clients
     *                    pass this via the debug gem's cookie handshake.
     * @param sessionName Optional friendly name surfaced to DAP clients.
     * @return 0 on success; negative RubyVMErrorCode on failure.
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun enableRemoteDebug(host: String?, port: Int, token: String, sessionName: String?): Int

    /**
     * Arm the remote line-eval console (sibling of [enableRemoteDebug]).
     * Connect with `nc 127.0.0.1 <port>` (or `rlwrap nc` for history). On
     * connect, send the token, then issue Ruby expressions to evaluate
     * against TOPLEVEL_BINDING (or a registered scope via
     * `RemoteEval.expose(:name, binding)` from script-side code).
     *
     * Same eager-boot constraint as [enableRemoteDebug]: must be called
     * before the first [enqueue] / `executeScriptSync`. In v1 the two
     * remote services are mutually exclusive within a single VM
     * lifecycle — whichever you call first arms its listener; the second
     * call returns the `ALREADY_STARTED` error code.
     *
     * @return 0 on success, negative RubyVMErrorCode on failure.
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun enableRemoteEval(host: String?, port: Int, token: String, sessionName: String?): Int

    /**
     * Destroy the interpreter and free all resources.
     * Must be called when the interpreter is no longer needed.
     *
     * After calling destroy(), this interpreter instance cannot be used.
     */
    fun destroy()

    /**
     * Close the interpreter, equivalent to destroy().
     * Needed for AutoCloseable and Closeable interfaces and for try-with-resources / .use{} calls.
     */
    @OptIn(ExperimentalStdlibApi::class)
    override fun close()

    companion object {
        /**
         * Create a new Ruby interpreter instance.
         *
         * @param appPath Working directory for script execution
         * @param rubyBaseDir Directory containing Ruby standard library
         * @param nativeLibsDir Directory containing native extensions
         * @param listener Callback for Ruby log output
         * @return A new RubyInterpreter instance
         * @throws RuntimeException if interpreter creation fails
         */
        fun create(
            appPath: String,
            rubyBaseDir: String,
            nativeLibsDir: String,
            listener: LogListener
        ): RubyInterpreter
    }
}
