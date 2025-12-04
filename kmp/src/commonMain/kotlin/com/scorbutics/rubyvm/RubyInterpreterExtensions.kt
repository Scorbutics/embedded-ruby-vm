package com.scorbutics.rubyvm

/**
 * Extension functions for RubyInterpreter providing convenient APIs
 * for script execution with automatic memory management.
 */

/**
 * Execute a Ruby script with automatic memory management.
 * The script is automatically closed after the callback completes.
 *
 * Example:
 * ```kotlin
 * interpreter.execute("puts 'Hello from Ruby!'") { exitCode ->
 *     println("Script completed with exit code: $exitCode")
 * }
 * ```
 *
 * @param scriptContent Ruby script content to execute
 * @param onComplete Callback invoked when script completes (receives exit code)
 */
fun RubyInterpreter.execute(scriptContent: String, onComplete: (Int) -> Unit) {
    val script = RubyScript.fromContent(scriptContent)
    enqueue(script) { exitCode ->
        try {
            onComplete(exitCode)
        } finally {
            script.close()
        }
    }
}
