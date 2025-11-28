package com.scorbutics.rubyvm

/**
 * Platform-specific native library loader.
 * Each platform (Android, Desktop) provides its own implementation.
 */
internal expect object NativeLibraryLoader {
    /**
     * Load the embedded-ruby native library.
     * Throws an exception if loading fails.
     */
    fun loadLibrary()
}
