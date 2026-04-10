package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.atomicfu.atomic
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.alloc
import kotlinx.cinterop.convert
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import platform.posix.*

/**
 * Stress test for Ruby VM GC interaction with Kotlin/Native GC.
 *
 * This test verifies that the Ruby VM can handle:
 * 1. Rapid script execution with concurrent Kotlin/Native and Ruby GC
 * 2. Memory pressure from both Ruby and Kotlin allocations
 * 3. Signal-safe interaction between the two runtimes
 */
@OptIn(ExperimentalForeignApi::class, kotlin.native.runtime.NativeRuntimeApi::class)
class RubyVMGCStressTest {

    @Test
    fun testRapidScriptExecutionWithGC() {
        println("=== Ruby VM GC Stress Test (Native) ===")
        println("This test rapidly executes scripts while forcing both Ruby and Kotlin/Native GC")
        println()

        val scriptCount = 50
        val completedCount = atomic(0)
        val failureCount = atomic(0)
        val latch = NativeCountDownLatch(scriptCount)

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
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = "./ruby",
            nativeLibsDir = "./lib",
            listener = listener
        )

        try {
            println("Starting stress test with $scriptCount scripts...")
            val startTime = getTimeMillis()

            repeat(scriptCount) { i ->
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

                // Trigger Kotlin/Native GC periodically during script submission
                if (i % 10 == 0) {
                    kotlin.native.runtime.GC.collect()
                    usleep(10_000u) // 10ms
                }

                // Small delay to stagger script submissions
                usleep(10_000u) // 10ms
            }

            // Force Kotlin/Native GC multiple times during execution
            repeat(10) { gcRound ->
                usleep(200_000u) // 200ms
                kotlin.native.runtime.GC.collect()
                println("  [GC Trigger $gcRound] Forced Kotlin/Native GC")
            }

            println("Waiting for scripts to complete...")
            val completed = latch.await(60)

            val duration = getTimeMillis() - startTime

            println()
            println("=== Test Results ===")
            println("Duration: ${duration}ms")
            println("Scripts completed: ${completedCount.value}/$scriptCount")
            println("Scripts failed: ${failureCount.value}")
            println("Timeout: ${!completed}")

            assertTrue(completed, "Test timed out waiting for scripts to complete")
            assertEquals(0, failureCount.value, "Some scripts failed")
            assertEquals(scriptCount, completedCount.value, "Not all scripts completed")

            println()
            println("Stress test PASSED - No crashes or failures!")

        } finally {
            println()
            println("Cleaning up interpreter...")
            interpreter.destroy()
            println("Cleanup complete")
        }
    }

    @Test
    fun testSynchronousExecutionWithGC() {
        println("=== Synchronous Execution GC Test (Native) ===")
        println("Tests enqueue with forced GC")
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
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = "./ruby",
            nativeLibsDir = "./lib",
            listener = listener
        )

        try {
            println("Executing scripts with GC...")
            val completedCount = atomic(0)
            val latch = NativeCountDownLatch(20)

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
                    usleep(1_000_000u) // 1s

                    println("  Script $i completed with exit code: $exitCode")
                    completedCount.incrementAndGet()
                    latch.countDown()
                    script.close()
                }

                // Force Kotlin/Native GC periodically
                if (i % 5 == 0) {
                    kotlin.native.runtime.GC.collect()
                    println("  [Forced Kotlin/Native GC after script $i]")
                }

                usleep(50_000u) // 50ms
            }

            println("Waiting for all callbacks to complete...")
            val completed = latch.await(60)
            assertTrue(completed, "Test timed out waiting for callbacks")

            println()
            println("Synchronous GC test PASSED (completed: ${completedCount.value}/20)")

        } finally {
            println()
            println("Cleaning up...")
            interpreter.destroy()
        }
    }

    @Test
    fun testMemoryPressureWithGC() {
        println("=== Memory Pressure + GC Test (Native) ===")
        println("Tests VM under memory pressure with frequent GC")
        println()

        val listener = object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                if (logMessage.isError()) {
                    println("[Ruby ERROR] ${logMessage.message}")
                }
            }
        }

        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = "./ruby",
            nativeLibsDir = "./lib",
            listener = listener
        )

        try {
            val latch = NativeCountDownLatch(10)
            val errors = atomic(0)

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

                // Aggressive Kotlin/Native GC
                kotlin.native.runtime.GC.collect()
                usleep(100_000u) // 100ms
            }

            assertTrue(latch.await(30), "Memory pressure test timed out")
            assertEquals(0, errors.value, "Scripts failed under memory pressure")

            println("Memory pressure test PASSED")

        } finally {
            interpreter.destroy()
        }
    }

    private fun getTimeMillis(): Long {
        return memScoped {
            val ts = alloc<timespec>()
            clock_gettime(CLOCK_MONOTONIC.convert(), ts.ptr)
            ts.tv_sec * 1000L + ts.tv_nsec / 1000000L
        }
    }
}
