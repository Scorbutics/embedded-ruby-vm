package com.scorbutics.rubyvm

/**
 * Native targets use cinterop/static linking, so there is no dynamic
 * library loading step to guard against.
 */
internal actual fun isNativeLibraryLoaded(): Boolean = false
