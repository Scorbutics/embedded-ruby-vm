package com.scorbutics.rubyvm

/**
 * Configuration for native library loading.
 * Allows customization of the library name to be loaded.
 */
object LibraryConfig {
    /**
     * The name of the native library to load (without "lib" prefix or extension).
     * Default is "embedded-ruby", but this can be changed for different applications.
     * 
     * For example:
     * - "embedded-ruby" loads libembedded-ruby.so (Android) or libembedded-ruby.dylib (iOS)
     * - "rgss_runtime" loads librgss_runtime.so (Android) or librgss_runtime.dylib (iOS)
     * - "myapp" loads libmyapp.so (Android) or libmyapp.dylib (iOS)
     */
    var libraryName: String = "embedded-ruby"
        set(value) {
            if (field != value && NativeLibraryLoader.isLoaded()) {
                throw IllegalStateException(
                    "Cannot change library name after library has been loaded. " +
                    "Set LibraryConfig.libraryName before any calls to RubyVMNative or NativeLibraryLoader."
                )
            }
            field = value
        }
}
