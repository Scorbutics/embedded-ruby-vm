package com.scorbutics.rubyvm

/**
 * Represents a Ruby script to be executed by the interpreter.
 *
 * Platform-specific implementations handle the actual script creation and lifecycle.
 */
@OptIn(ExperimentalStdlibApi::class)
expect class RubyScript : AutoCloseable {
    /**
     * Destroy the script and free associated resources.
     * Must be called when the script is no longer needed.
     */
    fun destroy()

    /**
     * Close the script, equivalent to destroy().
     * Needed for AutoCloseable and Closeable interfaces and for try-with-resources / .use{} calls.
     */
    @OptIn(ExperimentalStdlibApi::class)
    override fun close()

    companion object {
        /**
         * Create a script from Ruby source code content.
         *
         * @param content The Ruby source code as a string
         * @return A new RubyScript instance
         * @throws IllegalArgumentException if content is invalid
         */
        fun fromContent(content: String): RubyScript
    }
}
