package com.scorbutics.rubyvm

/**
 * Ruby interpreter that can execute Ruby scripts.
 *
 * The interpreter manages a Ruby VM instance and provides methods to
 * execute scripts asynchronously with callbacks.
 *
 * **Concurrency model.** Multiple [RubyInterpreter] instances can coexist within
 * a single process and execute scripts in parallel — each instance owns its own
 * Ruby thread, and scripts enqueued on a given instance run sequentially on that
 * instance's thread. Log output (Ruby `puts` / `STDERR.puts` / VMLogger) from a
 * script is delivered to the [LogListener] of the interpreter that script is
 * running on; cross-interpreter routing is not possible. The first interpreter's
 * paths (appPath, rubyBaseDir, nativeLibsDir) initialize process-global state;
 * subsequent interpreters share that initialization but otherwise operate
 * independently for execution and logging purposes.
 *
 * Platform implementations:
 * - Android/JVM: Uses JNI to call native C library
 * - iOS/macOS/Linux: Uses Kotlin/Native cinterop to call C directly
 *
 * Example usage:
 * ```kotlin
 * val listener = object : LogListener {
 *     override fun onLogMessage(logMessage: LogMessage) {
 *         val tag = if (logMessage.level == LogLevel.ERROR) "[Error]" else "[Ruby]"
 *         println("$tag ${logMessage.message}")
 *     }
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
     * Set the listener receiving the [HostEvent]s (`VMHost.emit`) of the
     * scripts this interpreter runs: they come with its log lines, so they
     * need the logging pipes the VM starts with once logging is enabled
     * (without them, `VMHost.emit` returns false). Replaces the previous
     * listener; null removes it: events not delivered yet go to the next
     * listener set, or nowhere if none is set when their turn comes.
     *
     * @throws IllegalStateException if interpreter has been destroyed
     */
    fun setHostEventListener(listener: HostEventListener?)

    /**
     * Arm the Ruby `debug` gem's DAP listener so a DAP client (VS Code's
     * rdbg extension, `rdbg --attach`, JetBrains) can attach for live
     * debugging — breakpoints, step in/over/out, locals, exceptions.
     *
     * Called before the VM is up, eagerly boots it with the listener armed
     * in non-stop mode, so the port is available before the first script is
     * enqueued. Called on a VM already up (after [enableRemoteEval], or after
     * a script), injects the listener through this interpreter's queue: it
     * then runs behind whatever script is queued, so call it before the
     * first [enqueue] / `executeScriptSync`.
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
     * Same boot-or-inject behaviour as [enableRemoteDebug]: call it before
     * the first [enqueue] / `executeScriptSync`. The two remote services
     * coexist on one VM, each on its own port: whichever is called first
     * boots the VM with its listener, the second injects its own into the
     * running VM.
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
