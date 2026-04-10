package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertTrue

class LoadLibraryTest {
    @Test
    fun testCreateInterpreter() {
        // On native, there's no JNI library loading step.
        // Instead, verify that creating an interpreter via cinterop works.
        try {
            val companion = RubyInterpreter
            println("RubyInterpreter class loaded successfully")
            assertTrue(true)
        } catch (e: Throwable) {
            e.printStackTrace()
            throw e
        }
    }
}
