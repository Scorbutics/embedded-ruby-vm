package com.scorbutics.rubyvm

/**
 * Represents the result of a Ruby script execution.
 *
 * Contains metadata about the script execution including exit code,
 * duration, and success status.
 *
 * @property index The index of the script in a batch execution (0-based)
 * @property name Optional name/identifier for the script
 * @property exitCode The exit code returned by the script (0 = success)
 * @property durationMs Duration of the script execution in milliseconds
 * @property success True if the script completed successfully (exitCode == 0)
 */
data class ScriptResult(
    val index: Int,
    val name: String?,
    val exitCode: Int,
    val durationMs: Long,
    val success: Boolean = exitCode == 0
)

/**
 * Configuration for a script to be executed.
 *
 * @property content The Ruby source code to execute
 * @property name Optional name/identifier for the script
 */
data class ScriptConfig(
    val content: String,
    val name: String? = null
)

/**
 * Sealed class representing the result of a script execution with detailed outcome information.
 */
sealed class ExecutionResult {
    /**
     * Script executed successfully.
     *
     * @property exitCode The exit code returned by the script
     * @property durationMs Duration of execution in milliseconds
     */
    data class Success(val exitCode: Int, val durationMs: Long) : ExecutionResult()

    /**
     * Script execution failed with an error.
     *
     * @property error The exception that caused the failure
     */
    data class Failure(val error: Throwable) : ExecutionResult()

    /**
     * Script execution timed out.
     *
     * @property timeoutSeconds The timeout value that was exceeded
     */
    data class Timeout(val timeoutSeconds: Long) : ExecutionResult()
}

/**
 * Extension to calculate metrics from a list of script results.
 */
data class ExecutionMetrics(
    val totalScripts: Int,
    val successCount: Int,
    val failureCount: Int,
    val averageDurationMs: Long,
    val totalDurationMs: Long
)

/**
 * Convert a list of ScriptResults to ExecutionMetrics.
 */
fun List<ScriptResult>.toMetrics(): ExecutionMetrics {
    return ExecutionMetrics(
        totalScripts = size,
        successCount = count { it.success },
        failureCount = count { !it.success },
        averageDurationMs = if (isNotEmpty()) sumOf { it.durationMs } / size else 0,
        totalDurationMs = sumOf { it.durationMs }
    )
}
