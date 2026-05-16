package com.scorbutics.rubyvm

/**
 * Represents a log message with its source and severity.
 *
 * @property message The log message content
 * @property source The source pipe the message came from (which physical stream)
 * @property level The severity reported for this line. For VMLOGGER, recovered
 * from the in-band tag emitted by VMLogger.debug/info/error; for other sources,
 * derived from the stream (stderr -> ERROR, stdout -> INFO).
 */
data class LogMessage(
    val message: String,
    val source: LogSource,
    val level: LogLevel = LogLevel.INFO
) {
    /**
     * Convenience method to check if this is a Ruby stdout message
     */
    fun isRubyStdout(): Boolean = source == LogSource.RUBY_STDOUT

    /**
     * Convenience method to check if this is a Ruby stderr message
     */
    fun isRubyStderr(): Boolean = source == LogSource.RUBY_STDERR

    /**
     * Convenience method to check if this is a VMLogger message
     */
    fun isVMLogger(): Boolean = source == LogSource.VMLOGGER

    /**
     * Convenience method to check if this is a native stdout message
     */
    fun isNativeStdout(): Boolean = source == LogSource.NATIVE_STDOUT

    /**
     * Convenience method to check if this is a native stderr message
     */
    fun isNativeStderr(): Boolean = source == LogSource.NATIVE_STDERR

    /**
     * Convenience method to check if this is any error stream
     */
    fun isError(): Boolean = source == LogSource.RUBY_STDERR || source == LogSource.NATIVE_STDERR

    /**
     * Convenience method to check if this is any standard output stream
     */
    fun isStdout(): Boolean = source == LogSource.RUBY_STDOUT || source == LogSource.NATIVE_STDOUT
}
