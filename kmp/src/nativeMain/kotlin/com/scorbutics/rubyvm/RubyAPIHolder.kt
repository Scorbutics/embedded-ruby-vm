package com.scorbutics.rubyvm

import kotlinx.cinterop.ExperimentalForeignApi

/**
 * Holder to track whether Ruby VM is initialized.
 *
 * For static builds: Functions are directly linked, no API loading needed.
 * For dynamic builds: This would hold the loaded API (future enhancement).
 */
@OptIn(ExperimentalForeignApi::class)
internal object RubyAPIHolder {
    private var initialized = false

    /**
     * Mark the Ruby VM as initialized
     */
    fun setInitialized() {
        initialized = true
    }

    /**
     * Check if the Ruby VM has been initialized
     */
    fun isInitialized(): Boolean = initialized
}
