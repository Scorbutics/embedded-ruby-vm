package com.scorbutics.rubyvm

import java.io.InputStream

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

    /**
     * Android doesn't need loadFromStream since libraries are packaged in the APK.
     * This method is provided for compatibility with jvmMain code.
     */
    internal fun loadFromStream(inputStream: InputStream, libraryFileName: String) {
        throw UnsupportedOperationException(
            "loadFromStream is not supported on Android. " +
            "Libraries are automatically loaded from the APK's lib directory."
        )
    }
}
