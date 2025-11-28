package com.scorbutics.rubyvm

/**
 * Android implementation of RubyScript using JNI.
 */
actual class RubyScript internal constructor(
    internal val scriptPtr: Long
) {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed) {
            RubyVMNative.destroyScript(scriptPtr)
            isDestroyed = true
        }
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

/**
 * Native method declarations
 */
private object RubyVMNative {
    external fun createScript(content: String): Long
    external fun destroyScript(scriptPtr: Long)

    init {
        System.loadLibrary("embedded-ruby")
    }
}
