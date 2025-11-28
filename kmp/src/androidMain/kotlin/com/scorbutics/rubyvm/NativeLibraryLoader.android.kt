package com.scorbutics.rubyvm

/**
 * Android implementation of native library loader.
 * Uses the standard System.loadLibrary() which loads from APK's lib directory.
 */
internal actual object NativeLibraryLoader {
    actual fun loadLibrary() {
        System.loadLibrary("embedded-ruby")
    }
}
