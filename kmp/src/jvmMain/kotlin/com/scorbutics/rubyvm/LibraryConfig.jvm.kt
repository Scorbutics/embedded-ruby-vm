package com.scorbutics.rubyvm

internal actual fun isNativeLibraryLoaded(): Boolean = NativeLibraryLoader.isLoaded()
