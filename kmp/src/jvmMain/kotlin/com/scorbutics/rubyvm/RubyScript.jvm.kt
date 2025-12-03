package com.scorbutics.rubyvm

import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean

/**
 * JVM implementation of RubyScript using JNI.
 */
actual class RubyScript internal constructor(
    internal val scriptPtr: Long
) : Closeable, AutoCloseable {
    private val isDestroyed = AtomicBoolean(false)
    private val destroyLock = Any()

    actual fun destroy() {
        // Use compareAndSet to atomically check and set the flag
        if (isDestroyed.compareAndSet(false, true)) {
            synchronized(destroyLock) {
                RubyVMNative.destroyScript(scriptPtr)
            }
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
