package com.scorbutics.rubyvm

/**
 * Callback interface for Ruby VM log messages.
 *
 * Implement this interface to receive log output from different sources:
 * - Ruby stdout/stderr
 * - VMLogger (internal VM logging)
 * - Native C code stdout/stderr
 */
interface LogListener {
    /**
     * Called when a log message is received from any source.
     *
     * @param logMessage The log message with its source
     */
    fun onLogMessage(logMessage: LogMessage)

    /**
     * Legacy method: Called when Ruby outputs to stdout (via puts, print, etc.)
     *
     * @deprecated Use onLogMessage instead for better source tracking
     * @param message The log message
     */
    @Deprecated(
        message = "Use onLogMessage(LogMessage) instead",
        replaceWith = ReplaceWith("onLogMessage(LogMessage(message, LogSource.RUBY_STDOUT))")
    )
    fun onLog(message: String) {
        // Default implementation calls new method for backward compatibility
        onLogMessage(LogMessage(message, LogSource.RUBY_STDOUT))
    }

    /**
     * Legacy method: Called when Ruby outputs to stderr or encounters an error
     *
     * @deprecated Use onLogMessage instead for better source tracking
     * @param message The error message
     */
    @Deprecated(
        message = "Use onLogMessage(LogMessage) instead",
        replaceWith = ReplaceWith("onLogMessage(LogMessage(message, LogSource.RUBY_STDERR))")
    )
    fun onError(message: String) {
        // Default implementation calls new method for backward compatibility
        onLogMessage(LogMessage(message, LogSource.RUBY_STDERR))
    }
}
