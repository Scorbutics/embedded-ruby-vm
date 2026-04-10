package com.scorbutics.rubyvm

/**
 * JVM-specific native library loader.
 * Each JVM sub-platform (Android, Desktop) provides its own implementation.
 */
internal expect object NativeLibraryLoader {
    /**
     * Load the native library configured in LibraryConfig.
     * Throws an exception if loading fails.
     */
    fun loadLibrary()

    /**
     * Check if the native library has been loaded.
     */
    fun isLoaded(): Boolean
}
