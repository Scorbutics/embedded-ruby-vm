package com.scorbutics.rubyvm

import kotlinx.atomicfu.atomic
import kotlinx.atomicfu.update
import kotlin.native.concurrent.freeze

/**
 * Native-specific extension functions for RubyInterpreter.
 * Provides utilities for coordinating multiple scripts using atomic counters.
 */

/**
 * Simple countdown coordinator for Native platforms.
 * Similar to Java's CountDownLatch but using atomics.
 */
class NativeCountDownLatch(private val count: Int) {
    private val current = atomic(count)
    
    /**
     * Decrement the counter by one.
     */
    fun countDown() {
        current.update { it - 1 }
    }
    
    /**
     * Wait (busy-wait) until counter reaches zero.
     * Note: This is a simple busy-wait implementation.
     * For production use, consider platform-specific wait mechanisms.
     */
    fun await() {
        while (current.value > 0) {
            // Simple spin-wait
            kotlinx.cinterop.usleep(1000u) // 1ms sleep
        }
    }
}

/**
 * Execute a Ruby script and signal a latch when complete.
 * Useful for coordinating multiple scripts on Native platforms.
 *
 * Example:
 * ```kotlin
 * val latch = NativeCountDownLatch(2)
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
 * @param latch NativeCountDownLatch to signal upon completion
 * @param onComplete Optional callback invoked when script completes
 */
fun RubyInterpreter.execute(
    scriptContent: String,
    latch: NativeCountDownLatch,
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
