package com.scorbutics.rubyvm

import java.util.concurrent.CountDownLatch

/**
 * JVM-specific extension functions for RubyInterpreter.
 * Provides utilities for coordinating multiple scripts using CountDownLatch.
 */

/**
 * Execute a Ruby script and signal a latch when complete.
 * Useful for coordinating multiple scripts on JVM platforms.
 *
 * Example:
 * ```kotlin
 * val latch = CountDownLatch(2)
 * 
 * interpreter.execute("puts 'Script 1'", latch) { exitCode ->
 *     println("Script 1 completed: $exitCode")
 * }
 * 
 * interpreter.execute("puts 'Script 2'", latch) { exitCode ->
 *     println("Script 2 completed: $exitCode")
 * }
 * 
 * latch.await() // Wait for both scripts
 * ```
 *
 * @param scriptContent Ruby script content to execute
 * @param latch CountDownLatch to signal upon completion
 * @param onComplete Optional callback invoked when script completes
 */
fun RubyInterpreter.execute(
    scriptContent: String,
    latch: CountDownLatch,
    onComplete: ((Int) -> Unit)? = null
) {
    val script = RubyScript.fromContent(scriptContent)
    enqueue(script) { exitCode ->
        try {
            onComplete?.invoke(exitCode)
        } finally {
            script.close()
            latch.countDown()
        }
    }
}
