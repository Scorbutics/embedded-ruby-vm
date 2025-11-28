package com.scorbutics.rubyvm

/**
 * Ruby interpreter that can execute Ruby scripts.
 *
 * The interpreter manages a Ruby VM instance and provides methods to
 * execute scripts asynchronously with callbacks.
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
expect class RubyInterpreter {
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
     * Destroy the interpreter and free all resources.
     * Must be called when the interpreter is no longer needed.
     *
     * After calling destroy(), this interpreter instance cannot be used.
     */
    fun destroy()

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
