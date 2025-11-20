package com.scorbutics.rubyvm

/**
 * Callback interface for Ruby VM log messages.
 *
 * Implement this interface to receive stdout and stderr output from Ruby scripts.
 */
interface LogListener {
    /**
     * Called when Ruby outputs to stdout (via puts, print, etc.)
     *
     * @param message The log message
     */
    fun onLog(message: String)

    /**
     * Called when Ruby outputs to stderr or encounters an error
     *
     * @param message The error message
     */
    fun onError(message: String)
}
