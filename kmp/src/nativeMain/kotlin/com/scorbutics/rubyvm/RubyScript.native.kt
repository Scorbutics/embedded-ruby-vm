package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*

/**
 * Native (iOS/macOS/Linux) implementation of RubyScript using cinterop.
 *
 * This implementation directly calls C functions through Kotlin/Native's cinterop.
 */
@OptIn(ExperimentalForeignApi::class)
actual class RubyScript internal constructor(
    internal val scriptPtr: CPointer<ruby_script>?
) {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed && scriptPtr != null) {
            ruby_script_destroy(scriptPtr)
            isDestroyed = true
        }
    }

    actual companion object {
        actual fun fromContent(content: String): RubyScript {
            require(content.isNotBlank()) { "Script content cannot be blank" }

            val scriptPtr = ruby_script_create(content)
            require(scriptPtr != null) { "Failed to create Ruby script" }

            return RubyScript(scriptPtr)
        }
    }
}
