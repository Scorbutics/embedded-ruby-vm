package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*

/**
 * Native (iOS/macOS/Linux) implementation of RubyScript using cinterop.
 *
 * For static builds: Calls C functions directly (statically linked).
 * For dynamic builds: Would use dlopen/dlsym (future enhancement).
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalStdlibApi::class)
actual class RubyScript internal constructor(
    internal val scriptPtr: COpaquePointer?
) : AutoCloseable {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed && scriptPtr != null) {
            // Call C function directly (works for static builds)
            ruby_script_destroy(scriptPtr.reinterpret())
            isDestroyed = true
        }
    }

    @OptIn(ExperimentalStdlibApi::class)
    actual override fun close() {
        destroy()
    }

    actual companion object {
        actual fun fromContent(content: String): RubyScript {
            require(content.isNotBlank()) { "Script content cannot be blank" }

            // Call C function directly (works for static builds)
            // Kotlin/Native will automatically convert String to const char*
            val scriptPtr = ruby_script_create_from_content(
                content,
                content.length.toULong()
            )
            require(scriptPtr != null) { "Failed to create Ruby script" }

            return RubyScript(scriptPtr)
        }
    }
}
