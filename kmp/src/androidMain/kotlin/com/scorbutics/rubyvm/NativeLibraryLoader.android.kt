package com.scorbutics.rubyvm

/**
 * Android implementation of native library loader.
 * Uses the standard System.loadLibrary() which loads from APK's lib directory.
 */
internal actual object NativeLibraryLoader {
    private var loaded = false
    
    @Synchronized
    actual fun loadLibrary() {
        if (loaded) {
            return
        }
        System.loadLibrary(LibraryConfig.libraryName)
        loaded = true
    }
    
    actual fun isLoaded(): Boolean = loaded
}
