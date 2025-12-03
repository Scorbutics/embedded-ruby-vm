package com.scorbutics.rubyvm

/**
 * JVM implementation of RubyScript using JNI.
 */
actual class RubyScript internal constructor(
    internal val scriptPtr: Long
) : AutoCloseable {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed) {
            RubyVMNative.destroyScript(scriptPtr)
            isDestroyed = true
        }
    }

    actual override fun close() {
        destroy()
    }

    actual companion object {
        actual fun fromContent(content: String): RubyScript {
            require(content.isNotBlank()) { "Script content cannot be blank" }

            val scriptPtr = RubyVMNative.createScript(content)
            require(scriptPtr != 0L) { "Failed to create Ruby script" }

            return RubyScript(scriptPtr)
        }
    }
}
