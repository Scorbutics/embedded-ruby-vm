package com.scorbutics.rubyvm

import platform.posix.clock_gettime
import platform.posix.CLOCK_MONOTONIC
import platform.posix.timespec

/**
 * Native implementation of ScriptBatch for executing multiple Ruby scripts.
 *
 * This builder provides a fluent API for batch script execution with
 * configurable timeouts and callbacks.
 */
actual class ScriptBatch internal constructor(
    private val interpreter: RubyInterpreter
) {
    private val scripts = mutableListOf<ScriptConfig>()
    private var timeoutSeconds: Long = 30
    private var onScriptComplete: ((Int, ScriptResult) -> Unit)? = null
    private var onBatchComplete: ((List<ScriptResult>) -> Unit)? = null

    /**
     * Add a script to the batch.
     *
     * @param content Ruby source code to execute
     * @param name Optional name/identifier for the script
     * @return This ScriptBatch instance for chaining
     */
    actual fun addScript(content: String, name: String?): ScriptBatch {
        scripts.add(ScriptConfig(content, name))
        return this
    }

    /**
     * Set the timeout for batch execution.
     *
     * @param seconds Maximum time to wait for all scripts to complete
     * @return This ScriptBatch instance for chaining
     */
    actual fun timeout(seconds: Long): ScriptBatch {
        timeoutSeconds = seconds
        return this
    }

    /**
     * Set a callback to be invoked when each script completes.
     *
     * @param callback Function called with script index and result
     * @return This ScriptBatch instance for chaining
     */
    actual fun onEachComplete(callback: (index: Int, result: ScriptResult) -> Unit): ScriptBatch {
        onScriptComplete = callback
        return this
    }

    /**
     * Set a callback to be invoked when all scripts complete.
     *
     * @param callback Function called with list of all results
     * @return This ScriptBatch instance for chaining
     */
    actual fun onComplete(callback: (results: List<ScriptResult>) -> Unit): ScriptBatch {
        onBatchComplete = callback
        return this
    }

    /**
     * Execute all scripts in the batch and wait for completion.
     *
     * Scripts are executed concurrently (enqueued immediately), and this method
     * blocks until all scripts complete or the timeout is reached.
     *
     * @return List of ScriptResult for each script, ordered by index
     * @throws IllegalStateException if timeout is exceeded
     */
    actual fun execute(): List<ScriptResult> {
        if (scripts.isEmpty()) {
            return emptyList()
        }

        val latch = NativeCountDownLatch(scripts.size)
        val results = MutableList<ScriptResult?>(scripts.size) { null }

        scripts.forEachIndexed { index, config ->
            val startTime = getTimeMillis()
            val script = RubyScript.fromContent(config.content)

            interpreter.enqueue(script) { exitCode ->
                try {
                    val result = ScriptResult(
                        index = index,
                        name = config.name,
                        exitCode = exitCode,
                        durationMs = getTimeMillis() - startTime,
                        success = exitCode == 0
                    )

                    results[index] = result
                    onScriptComplete?.invoke(index, result)
                } finally {
                    script.close()
                    latch.countDown()
                }
            }
        }

        val completed = latch.await(timeoutSeconds)
        if (!completed) {
            throw IllegalStateException(
                "Scripts did not complete within $timeoutSeconds seconds. " +
                "Completed: ${results.count { it != null }}/${scripts.size}"
            )
        }

        @Suppress("UNCHECKED_CAST")
        val finalResults = results as List<ScriptResult>
        onBatchComplete?.invoke(finalResults)

        return finalResults
    }

    /**
     * Get current time in milliseconds using platform-specific clock.
     */
    private fun getTimeMillis(): Long {
        val ts = timespec()
        clock_gettime(CLOCK_MONOTONIC, ts.ptr)
        return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L
    }
}
