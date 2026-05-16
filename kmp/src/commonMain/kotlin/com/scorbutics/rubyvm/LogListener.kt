package com.scorbutics.rubyvm

/**
 * Callback interface for Ruby VM log messages.
 *
 * Every log line — stdout/stderr from Ruby and native code, and VMLogger
 * output — is delivered through this single method. Use [LogMessage.source]
 * to discriminate which pipe produced the line and [LogMessage.level] to
 * distinguish VMLogger.debug / .info / .error.
 */
interface LogListener {
    /**
     * Called when a log message is received from any source.
     */
    fun onLogMessage(logMessage: LogMessage)
}
