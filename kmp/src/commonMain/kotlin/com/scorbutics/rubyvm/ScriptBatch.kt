package com.scorbutics.rubyvm

/**
 * Builder for executing multiple Ruby scripts in a batch.
 *
 * Provides a fluent API for configuring and executing multiple scripts with
 * callbacks for individual and batch completion.
 *
 * Example usage:
 * ```kotlin
 * val results = interpreter.batch()
 *     .addScript("puts 'Hello 1'", name = "greeting1")
 *     .addScript("puts 'Hello 2'", name = "greeting2")
 *     .timeout(60)
 *     .onEachComplete { index, result ->
 *         println("Script $index completed in ${result.durationMs}ms")
 *     }
 *     .execute()
 * ```
 */
expect class ScriptBatch {
    /**
     * Add a script to the batch.
     *
     * @param content Ruby source code to execute
     * @param name Optional name/identifier for the script
     * @return This ScriptBatch instance for chaining
     */
    fun addScript(content: String, name: String? = null): ScriptBatch

    /**
     * Set the timeout for batch execution.
     *
     * @param seconds Maximum time to wait for all scripts to complete
     * @return This ScriptBatch instance for chaining
     */
    fun timeout(seconds: Long): ScriptBatch

    /**
     * Set a callback to be invoked when each script completes.
     *
     * @param callback Function called with script index and result
     * @return This ScriptBatch instance for chaining
     */
    fun onEachComplete(callback: (index: Int, result: ScriptResult) -> Unit): ScriptBatch

    /**
     * Set a callback to be invoked when all scripts complete.
     *
     * @param callback Function called with list of all results
     * @return This ScriptBatch instance for chaining
     */
    fun onComplete(callback: (results: List<ScriptResult>) -> Unit): ScriptBatch

    /**
     * Execute all scripts in the batch and wait for completion.
     *
     * @return List of ScriptResult for each script, ordered by index
     * @throws IllegalStateException if timeout is exceeded
     */
    fun execute(): List<ScriptResult>
}
