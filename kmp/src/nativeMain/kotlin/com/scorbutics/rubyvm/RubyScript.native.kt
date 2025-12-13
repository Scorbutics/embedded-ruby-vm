package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*

/**
 * Native (iOS/macOS/Linux) implementation of RubyScript using cinterop.
 *
 * This implementation uses dynamic library loading via the RubyAPI.
 * Note: RubyInterpreter.create() must be called at least once before creating scripts
 * to ensure the Ruby API is loaded.
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalStdlibApi::class)
actual class RubyScript internal constructor(
    internal val scriptPtr: COpaquePointer?
) : AutoCloseable {
    private var isDestroyed = false

    actual fun destroy() {
        if (!isDestroyed && scriptPtr != null) {
            val api = RubyAPIHolder.getAPI()
            val destroyFunc = api.script.destroy
            requireNotNull(destroyFunc) { "destroy function not loaded from API" }

            destroyFunc(scriptPtr.reinterpret())
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

            val api = RubyAPIHolder.getAPI()
            val createFunc = api.script.create_from_content
            requireNotNull(createFunc) { "create_from_content function not loaded from API" }

            memScoped {
                val scriptPtr = createFunc(content.cstr.ptr, content.length.toULong())
                require(scriptPtr != null) { "Failed to create Ruby script" }

                return RubyScript(scriptPtr)
            }
        }
    }
}
