package com.scorbutics.rubyvm

/**
 * Identifies the source of a log message.
 *
 * Different sources allow you to handle logs differently based on where they originated:
 * - RUBY_STDOUT: Standard Ruby output (puts, print, p)
 * - RUBY_STDERR: Ruby error output (warn, raise)
 * - VMLOGGER: Internal VM logging (VMLogger.debug/info/error)
 * - NATIVE_STDOUT: Native C code stdout
 * - NATIVE_STDERR: Native C code stderr
 */
enum class LogSource {
    /**
     * Ruby VM standard output (puts, print, p)
     */
    RUBY_STDOUT,

    /**
     * Ruby VM standard error (warn, raise)
     */
    RUBY_STDERR,

    /**
     * VMLogger output (VMLogger.debug/info/error)
     */
    VMLOGGER,

    /**
     * Native C code stdout
     */
    NATIVE_STDOUT,

    /**
     * Native C code stderr
     */
    NATIVE_STDERR
}
