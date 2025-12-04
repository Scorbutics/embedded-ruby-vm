package com.scorbutics.rubyvm

import kotlinx.atomicfu.atomic
import kotlinx.atomicfu.update
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.alloc
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.addressOf
import platform.posix.*

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
            usleep(1000u) // 1ms sleep
        }
    }

    /**
     * Wait with timeout until counter reaches zero.
     *
     * @param timeoutSeconds Maximum time to wait in seconds
     * @return true if count reached zero, false if timeout occurred
     */
    fun await(timeoutSeconds: Long): Boolean {
        val startTime = getTimeMillis()
        val timeoutMs = timeoutSeconds * 1000

        while (current.value > 0) {
            if (getTimeMillis() - startTime >= timeoutMs) {
                return false
            }
            usleep(1000u) // 1ms sleep
        }
        return true
    }
}

/**
 * Get current time in milliseconds using platform-specific clock.
 */
@OptIn(ExperimentalForeignApi::class)
private fun getTimeMillis(): Long {
    return memScoped {
        val ts = alloc<timespec>()
        clock_gettime(CLOCK_MONOTONIC, ts.ptr)
        ts.tv_sec * 1000L + ts.tv_nsec / 1000000L
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

/**
 * Execute multiple Ruby scripts in a batch and wait for all to complete.
 *
 * This is a convenience method that eliminates manual NativeCountDownLatch management.
 *
 * Example:
 * ```kotlin
 * val results = interpreter.executeBatch(
 *     scripts = listOf("puts 'Hello 1'", "puts 'Hello 2'"),
 *     timeoutSeconds = 30
 * ) { index, exitCode ->
 *     println("Script $index completed: $exitCode")
 * }
 * ```
 *
 * @param scripts List of Ruby script content strings to execute
 * @param timeoutSeconds Maximum time to wait for all scripts (default: 30 seconds)
 * @param onEachComplete Optional callback invoked when each script completes
 * @return List of exit codes in the same order as input scripts
 * @throws IllegalStateException if timeout is exceeded
 */
fun RubyInterpreter.executeBatch(
    scripts: List<String>,
    timeoutSeconds: Long = 30,
    onEachComplete: ((index: Int, exitCode: Int) -> Unit)? = null
): List<Int> {
    if (scripts.isEmpty()) return emptyList()

    val latch = NativeCountDownLatch(scripts.size)
    val exitCodes = MutableList(scripts.size) { -1 }

    scripts.forEachIndexed { index, scriptContent ->
        execute(scriptContent, latch) { exitCode ->
            exitCodes[index] = exitCode
            onEachComplete?.invoke(index, exitCode)
        }
    }

    val completed = latch.await(timeoutSeconds)
    if (!completed) {
        throw IllegalStateException("Scripts did not complete within $timeoutSeconds seconds")
    }

    return exitCodes
}

/**
 * Execute a Ruby script synchronously (blocking).
 *
 * This method blocks the calling thread until the script completes.
 *
 * Example:
 * ```kotlin
 * val exitCode = interpreter.executeSync("puts 'Hello'", timeoutSeconds = 10)
 * println("Script completed with exit code: $exitCode")
 * ```
 *
 * @param scriptContent Ruby script content to execute
 * @param timeoutSeconds Maximum time to wait for script completion (default: 30 seconds)
 * @return Exit code from the script
 * @throws IllegalStateException if timeout is exceeded
 */
fun RubyInterpreter.executeSync(scriptContent: String, timeoutSeconds: Long = 30): Int {
    val latch = NativeCountDownLatch(1)
    var exitCode = -1

    execute(scriptContent, latch) { code ->
        exitCode = code
    }

    val completed = latch.await(timeoutSeconds)
    if (!completed) {
        throw IllegalStateException("Script did not complete within $timeoutSeconds seconds")
    }

    return exitCode
}

/**
 * Execute a Ruby script and return a detailed result.
 *
 * This method provides more information than just an exit code, including
 * execution duration and structured error handling.
 *
 * Example:
 * ```kotlin
 * when (val result = interpreter.executeWithResult("puts 'Hello'")) {
 *     is ExecutionResult.Success -> println("Success in ${result.durationMs}ms")
 *     is ExecutionResult.Failure -> println("Error: ${result.error}")
 *     is ExecutionResult.Timeout -> println("Timed out after ${result.timeoutSeconds}s")
 * }
 * ```
 *
 * @param scriptContent Ruby script content to execute
 * @param timeoutSeconds Maximum time to wait for script completion (default: 30 seconds)
 * @return ExecutionResult with detailed outcome information
 */
fun RubyInterpreter.executeWithResult(
    scriptContent: String,
    timeoutSeconds: Long = 30
): ExecutionResult {
    val latch = NativeCountDownLatch(1)
    var result: ExecutionResult? = null
    val startTime = getTimeMillis()

    try {
        execute(scriptContent, latch) { exitCode ->
            result = ExecutionResult.Success(
                exitCode = exitCode,
                durationMs = getTimeMillis() - startTime
            )
        }

        val completed = latch.await(timeoutSeconds)
        return if (completed) {
            result ?: ExecutionResult.Failure(IllegalStateException("No result received"))
        } else {
            ExecutionResult.Timeout(timeoutSeconds)
        }
    } catch (e: Exception) {
        return ExecutionResult.Failure(e)
    }
}

/**
 * Execute a Ruby script from a file.
 *
 * Reads the file content and executes it as a Ruby script.
 *
 * Example:
 * ```kotlin
 * interpreter.executeFile("scripts/my_script.rb") { exitCode ->
 *     println("Script completed: $exitCode")
 * }
 * ```
 *
 * @param filePath Path to the Ruby script file
 * @param onComplete Callback invoked when script completes (receives exit code)
 * @throws IllegalArgumentException if the file doesn't exist or cannot be read
 */
@OptIn(ExperimentalForeignApi::class)
fun RubyInterpreter.executeFile(filePath: String, onComplete: (Int) -> Unit) {
    val content = readFileContent(filePath)
    execute(content, onComplete)
}

/**
 * Read file content using POSIX APIs.
 */
@OptIn(ExperimentalForeignApi::class)
private fun readFileContent(filePath: String): String {
    return memScoped {
        val file = fopen(filePath, "r")
            ?: throw IllegalArgumentException("Cannot open file: $filePath")

        try {
            // Get file size
            fseek(file, 0, SEEK_END)
            val size = ftell(file)
            rewind(file)

            if (size <= 0) {
                return ""
            }

            // Read content
            val buffer = ByteArray(size.toInt())
            buffer.usePinned { pinned ->
                val read = fread(pinned.addressOf(0), 1u, size.toULong(), file)
                if (read.toLong() != size) {
                    throw IllegalArgumentException("Failed to read file: $filePath")
                }
            }

            buffer.decodeToString()
        } finally {
            fclose(file)
        }
    }
}

/**
 * Create a batch builder for executing multiple scripts with advanced configuration.
 *
 * Example:
 * ```kotlin
 * val results = interpreter.batch()
 *     .addScript("puts 'Hello 1'", name = "greeting1")
 *     .addScript("puts 'Hello 2'", name = "greeting2")
 *     .timeout(60)
 *     .onEachComplete { index, result ->
 *         println("Script ${result.name} completed")
 *     }
 *     .execute()
 * ```
 *
 * @return A new ScriptBatch builder
 */
fun RubyInterpreter.batch(): ScriptBatch = ScriptBatch(this)
