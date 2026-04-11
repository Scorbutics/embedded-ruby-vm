package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.alloc
import kotlinx.cinterop.convert
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import platform.posix.*

/**
 * Smoke tests for Ruby VM on Kotlin/Native.
 *
 * These tests verify basic interpreter lifecycle and script execution.
 * Stress tests will be added once the basic flow is confirmed working.
 */
@OptIn(ExperimentalForeignApi::class)
class RubyVMSmokeTest {

    private fun createListener(): LogListener {
        return object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                val prefix = if (logMessage.isError()) "ERROR" else "LOG"
                println("  [$prefix][${logMessage.source}] ${logMessage.message}")
            }
        }
    }

    private fun getTimeMillis(): Long {
        return memScoped {
            val ts = alloc<timespec>()
            clock_gettime(CLOCK_MONOTONIC.convert(), ts.ptr)
            ts.tv_sec * 1000L + ts.tv_nsec / 1000000L
        }
    }

    @Test
    fun testCreateAndDestroyInterpreter() {
        println("=== Test: Create and Destroy Interpreter ===")

        val paths = RubyVMPaths.getDefaultPaths()
        println("  rubyBaseDir: ${paths.rubyBaseDir}")
        println("  nativeLibsDir: ${paths.nativeLibsDir}")

        println("  Creating interpreter...")
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = createListener()
        )
        println("  Interpreter created successfully")

        println("  Destroying interpreter...")
        interpreter.destroy()
        println("  Interpreter destroyed successfully")
    }

    @Test
    fun testExecuteSingleScript() {
        println("=== Test: Execute Single Script ===")

        val paths = RubyVMPaths.getDefaultPaths()
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = createListener()
        )

        try {
            val latch = NativeCountDownLatch(1)
            var resultCode = -1

            println("  Enqueuing script...")
            val script = RubyScript.fromContent("puts 'Hello from Ruby on Kotlin/Native!'")
            interpreter.enqueue(script) { exitCode ->
                println("  Script completed with exit code: $exitCode")
                resultCode = exitCode
                latch.countDown()
                script.close()
            }

            println("  Waiting for script (timeout: 30s)...")
            val completed = latch.await(30)

            assertTrue(completed, "Script did not complete within 30 seconds")
            assertEquals(0, resultCode, "Script exited with non-zero code")
            println("  Single script test PASSED")
        } finally {
            println("  Destroying interpreter...")
            interpreter.destroy()
            println("  Done")
        }
    }
}
