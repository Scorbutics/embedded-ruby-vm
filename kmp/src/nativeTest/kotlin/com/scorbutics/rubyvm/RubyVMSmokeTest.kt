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
 * Note: Ruby cannot be re-initialized after cleanup in the same process,
 * so all tests share a single interpreter lifecycle within one test method.
 */
@OptIn(ExperimentalForeignApi::class)
class RubyVMSmokeTest {

    private fun createListener(): LogListener {
        return object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                /* Print only errors to avoid feedback-looping into the
                 * logging system's redirected stdout pipe. See the contract
                 * in include/private/embedded-ruby-vm/logging.h: callbacks
                 * must not write to fd 1 / fd 2. The smoke test produces
                 * little output anyway, so dropping non-error println keeps
                 * the test functional without the recursion risk. */
                if (logMessage.isError()) {
                    println("  [ERROR][${logMessage.source}] ${logMessage.message}")
                }
            }
        }
    }

    @Test
    fun testInterpreterLifecycle() {
        println("=== Test: Interpreter Lifecycle ===")

        // Phase 1: Bootstrap and create
        println("  Bootstrapping Ruby VM paths...")
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

        try {
            // Phase 2: Execute a single script
            val latch = NativeCountDownLatch(1)
            var resultCode = -1

            val script = RubyScript.fromContent("puts 'Hello from Ruby on Kotlin/Native!'")
            println("  Enqueuing script...")
            fflush(null)

            interpreter.enqueue(script) { exitCode ->
                println("  Script completed with exit code: $exitCode")
                resultCode = exitCode
                latch.countDown()
                script.close()
            }

            println("  Enqueue returned, waiting for script (timeout: 30s)...")
            fflush(null)
            val completed = latch.await(30)

            assertTrue(completed, "Script did not complete within 30 seconds")
            assertEquals(0, resultCode, "Script exited with non-zero code")
            println("  Single script execution PASSED")

        } finally {
            // Phase 3: Destroy
            println("  Destroying interpreter...")
            interpreter.destroy()
            println("  Interpreter destroyed successfully")
        }
    }
}
