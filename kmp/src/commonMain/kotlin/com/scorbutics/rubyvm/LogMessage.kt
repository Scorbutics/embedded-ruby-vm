package com.scorbutics.rubyvm

/**
 * Sentinel interpreter id used by the C dispatcher when a line carries no
 * in-band TaggedIO tag — typically native C/C++ stdout, or anything that
 * wrote to the shared pipe outside a tagged Ruby worker context. Kept in
 * sync with `LOG_NATIVE_INTERPRETER_ID` in
 * `include/public/embedded-ruby-vm/log-listener.h`.
 */
const val LOG_NATIVE_INTERPRETER_ID: Int = 0

/**
 * Represents a log message with its source and severity.
 *
 * @property message The log message content
 * @property source The source pipe the message came from (which physical stream)
 * @property level The severity reported for this line. For VMLOGGER, recovered
 * from the in-band tag emitted by VMLogger.debug/info/error; for other sources,
 * derived from the stream (stderr -> ERROR, stdout -> INFO).
 * @property interpreterId Originating Ruby interpreter id, parsed by the C
 * dispatcher from the TaggedIO marker emitted by the producing worker Thread
 * (see fifo_interpreter.rb). Equals [LOG_NATIVE_INTERPRETER_ID] (0) when the
 * line did not carry a tag — i.e. native C/C++ output, or anything that
 * wrote to stdout outside a tagged worker. Consumers that need to filter
 * by producing interpreter should match on this field rather than try to
 * infer origin from message content.
 */
data class LogMessage(
    val message: String,
    val source: LogSource,
    val level: LogLevel = LogLevel.INFO,
    val interpreterId: Int = LOG_NATIVE_INTERPRETER_ID
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
