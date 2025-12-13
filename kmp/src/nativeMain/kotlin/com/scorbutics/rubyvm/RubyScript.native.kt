package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*

// Type alias to avoid naming conflict between Kotlin class and C struct
@OptIn(ExperimentalForeignApi::class)
internal typealias CRubyScript = com.scorbutics.rubyvm.native.RubyScript

/**
 * Native (iOS/macOS/Linux) implementation of RubyScript using cinterop.
 *
 * This implementation uses dynamic library loading via the RubyAPI.
 * Note: RubyInterpreter.create() must be called at least once before creating scripts
 * to ensure the Ruby API is loaded.
 */
@OptIn(ExperimentalForeignApi::class)
actual class RubyScript internal constructor(
    internal val scriptPtr: CPointer<CRubyScript>?
) : AutoCloseable {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed && scriptPtr != null) {
            val api = RubyAPIHolder.getAPI()
            val destroyFunc = api.script.destroy
            requireNotNull(destroyFunc) { "destroy function not loaded from API" }

            destroyFunc(scriptPtr)
            isDestroyed = true
        }
    }

    actual override fun close() {
        destroy()
    }

    actual companion object {
        actual fun fromContent(content: String): RubyScript {
            require(content.isNotBlank()) { "Script content cannot be blank" }

            val api = RubyAPIHolder.getAPI()
            val createFunc = api.script.create_from_content
            requireNotNull(createFunc) { "create_from_content function not loaded from API" }

            val scriptPtr = createFunc(content, content.length.toULong())
            require(scriptPtr != null) { "Failed to create Ruby script" }

            return RubyScript(scriptPtr.reinterpret())
        }
    }
}
