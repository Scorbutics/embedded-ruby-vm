package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertTrue

class LoadLibraryTest {
    @Test
    fun testLoadLibrary() {
        // This should trigger the init block of RubyInterpreter companion object
        // which loads the native library.
        // If it fails, it will throw an exception and fail the test.
        try {
            // We can't easily check if it's loaded without calling a native method,
            // but the init block runs on class loading.
            // Let's just access the companion object.
            val companion = RubyInterpreter
            println("RubyInterpreter class loaded successfully")
            assertTrue(true)
        } catch (e: Throwable) {
            e.printStackTrace()
            throw e
        }
    }
}
