package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.RubyAPI
import kotlinx.cinterop.ExperimentalForeignApi

/**
 * Global holder for the dynamically loaded Ruby API.
 * This allows RubyScript and other components to access the API without
 * passing it explicitly through every method call.
 *
 * The API is loaded once when the first RubyInterpreter is created.
 */
@OptIn(ExperimentalForeignApi::class)
internal object RubyAPIHolder {
    private var loadedAPI: RubyAPI? = null

    /**
     * Set the loaded API (called by RubyInterpreter when loading the library)
     */
    fun setAPI(api: RubyAPI) {
        loadedAPI = api
    }

    /**
     * Get the loaded API
     * @throws IllegalStateException if the API hasn't been loaded yet
     */
    fun getAPI(): RubyAPI {
        return loadedAPI ?: error(
            "Ruby API not loaded. Call RubyInterpreter.create() first to load the Ruby runtime."
        )
    }

    /**
     * Check if the API has been loaded
     */
    fun isLoaded(): Boolean = loadedAPI != null
}
