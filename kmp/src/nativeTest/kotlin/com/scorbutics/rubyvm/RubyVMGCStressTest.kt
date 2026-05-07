package com.scorbutics.rubyvm

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.atomicfu.atomic
import kotlinx.cinterop.ExperimentalForeignApi
import platform.posix.*

/**
 * Stress test for Ruby VM GC and signal handling on Kotlin/Native.
 *
 * This test verifies that the Ruby VM can handle:
 * 1. Rapid script execution with heavy Ruby object allocation
 * 2. Concurrent Ruby GC without crashes
 * 3. Signal masking preventing Ruby GC signals from crashing native threads
 *
 * Context: Ruby's GC uses signals (SIGPROF/SIGALRM) to pause threads during
 * marking phase. When these signals hit non-Ruby threads, they can cause segfaults.
 * This test ensures the signal masking in ruby_vm_execute_sync prevents this.
 *
 * Note: Ruby cannot be re-initialized after cleanup in the same process,
 * so all test phases share a single interpreter lifecycle within one test method.
 */
@OptIn(ExperimentalForeignApi::class)
class RubyVMGCStressTest {

    private fun createListener(): LogListener {
        return object : LogListener {
            override fun onLogMessage(logMessage: LogMessage) {
                /* IMPORTANT: callbacks MUST NOT write to fd 1 / fd 2 (println,
                 * System.out, printf...). That feedback-loops into the logging
                 * system's redirected stdout/stderr pipes, the dispatch worker
                 * keeps re-invoking us with growing strings, and the test
                 * eventually starves out under bounded-queue drops or stalls
                 * on a full pipe. See the contract in
                 * include/private/embedded-ruby-vm/logging.h.
                 *
                 * For this stress test we only care that scripts complete —
                 * we don't need to display per-script log lines. Print only
                 * actual errors, and even then keep volume low so the
                 * potentially-recursive output stays bounded by RECURSIVE-
                 * cap-of-one-line-per-error rather than per-script. */
                if (logMessage.isError()) {
                    println("  [ERROR][${logMessage.source}] ${logMessage.message}")
                }
            }
        }
    }

    @Test
    fun testGCStress() {
        println("=== Ruby VM GC Stress Test (Native) ===")

        val paths = RubyVMPaths.getDefaultPaths()
        val interpreter = RubyInterpreter.create(
            appPath = ".",
            rubyBaseDir = paths.rubyBaseDir,
            nativeLibsDir = paths.nativeLibsDir,
            listener = createListener()
        )

        try {
            // Phase 1: Rapid script execution with GC
            testRapidScriptExecutionWithGC(interpreter)

            // Phase 2: Synchronous execution with GC
            testSynchronousExecutionWithGC(interpreter)

            // Phase 3: Memory pressure with GC
            testMemoryPressureWithGC(interpreter)

        } finally {
            println()
            println("Cleaning up interpreter...")
            interpreter.destroy()
            println("Cleanup complete")
        }
    }

    private fun testRapidScriptExecutionWithGC(interpreter: RubyInterpreter) {
        println()
        println("--- Phase 1: Rapid Script Execution with GC ---")

        val scriptCount = 50
        val completedCount = atomic(0)
        val failureCount = atomic(0)
        val latch = NativeCountDownLatch(scriptCount)

        println("Starting stress test with $scriptCount scripts...")

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

            // Small delay to stagger script submissions
            usleep(10_000u) // 10ms
        }

        println("Waiting for scripts to complete...")
        val completed = latch.await(60)

        println()
        println("Phase 1 Results:")
        println("  Scripts completed: ${completedCount.value}/$scriptCount")
        println("  Scripts failed: ${failureCount.value}")
        println("  Timeout: ${!completed}")

        assertTrue(completed, "Test timed out waiting for scripts to complete")
        assertEquals(0, failureCount.value, "Some scripts failed")
        assertEquals(scriptCount, completedCount.value, "Not all scripts completed")

        println("  Phase 1 PASSED")
    }

    private fun testSynchronousExecutionWithGC(interpreter: RubyInterpreter) {
        println()
        println("--- Phase 2: Synchronous Execution with GC ---")

        val completedCount = atomic(0)
        val scriptCount = 20
        val latch = NativeCountDownLatch(scriptCount)

        repeat(scriptCount) { i ->
            val script = RubyScript.fromContent("""
                puts "Sync script $i starting"
                arr = []
                1000.times { |n| arr << "item_#{n}" }
                ${if (i % 3 == 0) "GC.start" else ""}
                puts "Sync script $i completed"
            """.trimIndent())

            interpreter.enqueue(script) { exitCode ->
                println("  Script $i completed with exit code: $exitCode")
                completedCount.incrementAndGet()
                latch.countDown()
                script.close()
            }

            usleep(50_000u) // 50ms
        }

        println("Waiting for all callbacks to complete...")
        val completed = latch.await(60)
        assertTrue(completed, "Test timed out waiting for callbacks")

        println()
        println("  Phase 2 PASSED (completed: ${completedCount.value}/$scriptCount)")
    }

    private fun testMemoryPressureWithGC(interpreter: RubyInterpreter) {
        println()
        println("--- Phase 3: Memory Pressure + GC ---")

        val scriptCount = 10
        val latch = NativeCountDownLatch(scriptCount)
        val errors = atomic(0)

        println("Executing memory-intensive scripts...")

        repeat(scriptCount) { i ->
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

            usleep(100_000u) // 100ms
        }

        assertTrue(latch.await(30), "Memory pressure test timed out")
        assertEquals(0, errors.value, "Scripts failed under memory pressure")

        println("  Phase 3 PASSED")
    }
}
