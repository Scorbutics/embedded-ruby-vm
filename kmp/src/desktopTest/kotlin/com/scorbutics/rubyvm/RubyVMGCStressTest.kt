package com.scorbutics.rubyvm

import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

/**
 * Stress test for Ruby VM GC and signal handling.
 *
 * This test verifies that the Ruby VM can handle:
 * 1. Rapid script execution from multiple JVM threads
 * 2. Concurrent Ruby GC and JVM GC without crashes
 * 3. Signal masking preventing Ruby GC signals from crashing JVM threads
 *
 * Context: Ruby's GC uses signals (SIGPROF/SIGALRM) to pause threads during
 * marking phase. When these signals hit JVM threads, they can cause segfaults.
 * This test ensures the signal masking in ruby_vm_execute_sync prevents this.
 *
 * Each test method is also wrapped in a UAF regression check: the
 * JNICallbackContext magic canary increments a process-global counter
 * whenever a logging callback observes a freed listener. We capture the
 * counter at @BeforeTest, drain the logging system at @AfterTest so any
 * still-buffered callbacks fire before we read, then assert the delta is 0.
 * Any future regression of the listener-lifetime race fixed by
 * logging_swap_listener will fail here loudly instead of silently corrupting
 * memory.
 */
class RubyVMGCStressTest {

    private var badMagicBaseline = 0L

    @BeforeTest
    fun captureBadMagicBaseline() {
        badMagicBaseline = RubyVMNative.getBadMagicCount()
    }

    @AfterTest
    fun assertNoBadMagic() {
        // Deterministic drain — pushes a barrier through the VMLOGGER pipe
        // and blocks until the logging thread has dispatched every callback
        // for data buffered up to this point. Replaces a Thread.sleep heuristic.
        val drainResult = RubyVMNative.drainLogging(2000)
        // -2 means "logging not running" which is also a valid drained state.
        assertTrue(
            drainResult == 0 || drainResult == -2,
            "logging drain failed (rc=$drainResult); test cannot reliably assert UAF status"
        )
        val delta = RubyVMNative.getBadMagicCount() - badMagicBaseline
        assertEquals(
            0L,
            delta,
            "JNICallbackContext UAF detected: $delta BAD magic event(s) fired during test " +
                "— see [JNI-DIAG] message_callback lines on stderr."
        )
    }

    @Test
    fun testRapidScriptExecutionWithGC() {
        println("=== Ruby VM GC Stress Test ===")
        println("This test rapidly executes scripts while forcing both Ruby and JVM GC")
        println()

        val scriptCount = 50
        val completedCount = AtomicInteger(0)
        val failureCount = AtomicInteger(0)
        val latch = CountDownLatch(scriptCount)

        // Simple log listener
        val listener = object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                if (logMessage.isError()) {
                    println("[Ruby ERROR] ${logMessage.message}")
                } else if (logMessage.message.contains("ERROR") || logMessage.message.contains("WARN")) {
                    println("[Ruby] ${logMessage.message}")
                }
            }
        }

        println("Creating interpreter...")
        val paths = RubyVMPaths.getDefaultPaths()
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = listener
        )

        try {
            println("Starting stress test with $scriptCount scripts...")
            val startTime = System.currentTimeMillis()

            // Launch rapid script executions
            repeat(scriptCount) { i ->
                // Create a script that does work and explicitly triggers GC
                val script = RubyScript.fromContent("""
                    # Script $i - Do some work and trigger GC
                    sum = 0
                    100.times do |n|
                      sum += n
                      # Allocate some objects to trigger GC
                      arr = Array.new(100) { |x| "string_#{x}_#{n}" }
                    end
                    
                    # Explicitly trigger Ruby GC on some scripts
                    ${if (i % 5 == 0) "GC.start" else "# No explicit GC"}
                    
                    puts "Script $i completed: sum=#{sum}"
                """.trimIndent())

                // Execute on separate thread to simulate real-world usage
                thread(name = "ScriptThread-$i") {
                    try {
                        interpreter.enqueue(script) { exitCode ->
                            if (exitCode == 0) {
                                completedCount.incrementAndGet()
                            } else {
                                failureCount.incrementAndGet()
                                println("Script $i FAILED with exit code: $exitCode")
                            }
                            latch.countDown()
                            script.close()
                        }

                        // Trigger JVM GC periodically during script execution
                        if (i % 10 == 0) {
                            System.gc()
                            Thread.sleep(10) // Give GC a moment
                        }

                    } catch (e: Exception) {
                        println("Exception in script $i: ${e.message}")
                        e.printStackTrace()
                        failureCount.incrementAndGet()
                        latch.countDown()
                        script.close()
                    }
                }

                // Small delay to stagger script submissions
                Thread.sleep(10)
            }

            // Force JVM GC multiple times during execution
            val gcThread = thread(name = "GC-Trigger") {
                repeat(10) {
                    Thread.sleep(200)
                    System.gc()
                    println("  [GC Trigger $it] Forced JVM GC")
                }
            }

            // Wait for all scripts to complete (with timeout)
            println("Waiting for scripts to complete...")
            val completed = latch.await(60, TimeUnit.SECONDS)
            
            gcThread.join(5000)

            val duration = System.currentTimeMillis() - startTime

            println()
            println("=== Test Results ===")
            println("Duration: ${duration}ms")
            println("Scripts completed: ${completedCount.get()}/$scriptCount")
            println("Scripts failed: ${failureCount.get()}")
            println("Timeout: ${!completed}")

            // Verify results
            assertTrue(completed, "Test timed out waiting for scripts to complete")
            assertEquals(0, failureCount.get(), "Some scripts failed")
            assertEquals(scriptCount, completedCount.get(), "Not all scripts completed")

            println()
            println("✓ Stress test PASSED - No crashes or failures!")
            println("  This indicates signal masking is working correctly.")

        } finally {
            println()
            println("Cleaning up interpreter...")
            interpreter.destroy()
            println("Cleanup complete")
        }
    }

    @Test
    fun testSynchronousExecutionWithGC() {
        println("=== Synchronous Execution GC Test ===")
        println("Tests executeScriptSync with forced GC")
        println()

        val listener = object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                if (logMessage.isError()) {
                    println("[Ruby ERROR] ${logMessage.message}")
                } else if (logMessage.message.contains("ERROR")) {
                    println("[Ruby] ${logMessage.message}")
                }
            }
        }

        println("Creating interpreter...")
        val paths = RubyVMPaths.getDefaultPaths()
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = listener
        )

        try {
            println("Executing scripts synchronously with GC...")
            val completedCount = AtomicInteger(0)
            val latch = CountDownLatch(20)

            repeat(20) { i ->
                val script = RubyScript.fromContent("""
                    puts "Sync script $i starting"
                    arr = []
                    1000.times { |n| arr << "item_#{n}" }
                    ${if (i % 3 == 0) "GC.start" else ""}
                    puts "Sync script $i completed"
                """.trimIndent())

                interpreter.enqueue(script) { exitCode ->
                    // Sleep after returning from native to expose crash window
                    Thread.sleep(1000)
                    
                    println("  Script $i completed with exit code: $exitCode")
                    completedCount.incrementAndGet()
                    latch.countDown()
                    script.close()
                }

                // Force JVM GC periodically
                if (i % 5 == 0) {
                    System.gc()
                    println("  [Forced JVM GC after script $i]")
                }

                Thread.sleep(50)
            }

            // Wait for all callbacks to complete
            println("Waiting for all callbacks to complete...")
            val completed = latch.await(60, TimeUnit.SECONDS)
            assertTrue(completed, "Test timed out waiting for callbacks")

            println()
            println("✓ Synchronous GC test PASSED (completed: ${completedCount.get()}/20)")

        } finally {
            println()
            println("Cleaning up...")
            interpreter.destroy()
        }
    }

    @Test
    fun testMemoryPressureWithGC() {
        println("=== Memory Pressure + GC Test ===")
        println("Tests VM under memory pressure with frequent GC")
        println()

        val listener = object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                if (logMessage.isError()) {
                    println("[Ruby ERROR] ${logMessage.message}")
                }
            }
        }

        val paths = RubyVMPaths.getDefaultPaths()
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = listener
        )

        try {
            val latch = CountDownLatch(10)
            val errors = AtomicInteger(0)

            println("Executing memory-intensive scripts...")

            repeat(10) { i ->
                val script = RubyScript.fromContent("""
                    # Allocate significant memory
                    big_arrays = []
                    10.times do
                      arr = Array.new(10000) { |n| "string_data_#{n}_#{rand(1000)}" }
                      big_arrays << arr
                      GC.start if rand(3) == 0
                    end
                    
                    # Force full GC
                    GC.start
                    
                    puts "Memory test $i complete"
                """.trimIndent())

                interpreter.enqueue(script) { exitCode ->
                    if (exitCode != 0) {
                        errors.incrementAndGet()
                    }
                    latch.countDown()
                    script.close()
                }

                // Aggressive JVM GC
                System.gc()
                Thread.sleep(100)
            }

            assertTrue(latch.await(30, TimeUnit.SECONDS), "Memory pressure test timed out")
            assertEquals(0, errors.get(), "Scripts failed under memory pressure")

            println("✓ Memory pressure test PASSED")

        } finally {
            interpreter.destroy()
        }
    }
}
