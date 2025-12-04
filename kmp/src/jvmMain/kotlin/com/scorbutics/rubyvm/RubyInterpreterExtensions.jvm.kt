package com.scorbutics.rubyvm

import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

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

/**
 * Execute multiple Ruby scripts in a batch and wait for all to complete.
 *
 * This is a convenience method that eliminates manual CountDownLatch management.
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

    val latch = CountDownLatch(scripts.size)
    val exitCodes = MutableList(scripts.size) { -1 }

    scripts.forEachIndexed { index, scriptContent ->
        execute(scriptContent, latch) { exitCode ->
            synchronized(exitCodes) {
                exitCodes[index] = exitCode
            }
            onEachComplete?.invoke(index, exitCode)
        }
    }

    val completed = latch.await(timeoutSeconds, TimeUnit.SECONDS)
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
    val latch = CountDownLatch(1)
    var exitCode = -1

    execute(scriptContent, latch) { code ->
        exitCode = code
    }

    val completed = latch.await(timeoutSeconds, TimeUnit.SECONDS)
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
    val latch = CountDownLatch(1)
    var result: ExecutionResult? = null
    val startTime = System.currentTimeMillis()

    try {
        execute(scriptContent, latch) { exitCode ->
            result = ExecutionResult.Success(
                exitCode = exitCode,
                durationMs = System.currentTimeMillis() - startTime
            )
        }

        val completed = latch.await(timeoutSeconds, TimeUnit.SECONDS)
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
 * @throws java.io.FileNotFoundException if the file doesn't exist
 */
fun RubyInterpreter.executeFile(filePath: String, onComplete: (Int) -> Unit) {
    val content = File(filePath).readText()
    execute(content, onComplete)
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
