package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/**
 * Test for Linux x64 native implementation using cinterop.
 */
class RubyVMNativeTest {

    @Test
    fun testCreateInterpreter() {
        // Simple test to verify cinterop bindings are working
        val listener = object : LogListener {
            val logs = mutableListOf<String>()
            val errors = mutableListOf<String>()

            override fun onLog(message: String) {
                println("Ruby Log: $message")
                logs.add(message)
            }

            override fun onError(message: String) {
                println("Ruby Error: $message")
                errors.add(message)
            }
        }

        // Create interpreter
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = "./ruby",
            nativeLibsDir = "./ruby/lib",
            listener = listener
        )

        assertNotNull(interpreter, "Interpreter should be created successfully")

        // Create and execute a simple Ruby script
        val script = RubyScript.fromContent("puts 'Hello from Ruby on Linux x64!'")
        assertNotNull(script, "Script should be created successfully")

        var completed = false
        var exitCode = -1

        interpreter.enqueue(script) { code ->
            completed = true
            exitCode = code
            println("Script completed with exit code: $code")
        }

        // Wait a bit for the script to execute
        // Note: In a real test, you'd want proper synchronization
        Thread.sleep(1000)

        assertTrue(completed, "Script should have completed")
        assertEquals(0, exitCode, "Script should exit with code 0")
        assertTrue(listener.logs.isNotEmpty(), "Should have received some logs")

        // Cleanup
        script.destroy()
        interpreter.destroy()

        println("Test completed successfully!")
    }

    @Test
    fun testScriptCreation() {
        val script = RubyScript.fromContent("puts 'Simple test'")
        assertNotNull(script, "Script should be created")
        script.destroy()
    }
}
