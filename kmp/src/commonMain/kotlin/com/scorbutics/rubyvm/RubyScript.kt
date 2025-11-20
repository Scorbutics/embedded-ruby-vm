package com.scorbutics.rubyvm

/**
 * Represents a Ruby script to be executed by the interpreter.
 *
 * Platform-specific implementations handle the actual script creation and lifecycle.
 */
expect class RubyScript {
    /**
     * Destroy the script and free associated resources.
     * Must be called when the script is no longer needed.
     */
    fun destroy()

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
