package examples

import com.scorbutics.rubyvm.*

/**
 * Example demonstrating the improved Ruby VM API with batch execution,
 * synchronous execution, and builder pattern.
 *
 * To run this example:
 * ../../gradlew runExample
 */
fun main() {
    println("=== Ruby VM Improved API Examples ===\n")

    val listener = object : LogListener {
        override fun onLog(message: String) {
            println("[Ruby] $message")
        }

        override fun onError(message: String) {
            System.err.println("[Ruby Error] $message")
        }
    }

    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./lib",
        listener = listener
    ).use { interpreter ->
        println("Interpreter created!\n")

        // Example 1: Simple batch execution (replaces manual CountDownLatch)
        println("=== Example 1: Simple Batch Execution ===")
        val exitCodes = interpreter.executeBatch(
            scripts = listOf(
                "puts 'Batch Script 1'",
                "puts 'Batch Script 2'",
                "puts 'Batch Script 3'"
            ),
            timeoutSeconds = 30
        ) { index, exitCode ->
            println("Batch script $index completed with exit code: $exitCode")
        }
        println("All exit codes: $exitCodes\n")

        // Example 2: Synchronous execution (blocking)
        println("=== Example 2: Synchronous Execution ===")
        val syncExitCode = interpreter.executeSync(
            scriptContent = """
                puts "This is a synchronous script"
                puts "It blocks until completion"
                42 # Return value
            """.trimIndent(),
            timeoutSeconds = 10
        )
        println("Sync script completed with exit code: $syncExitCode\n")

        // Example 3: Execution with detailed result
        println("=== Example 3: Execution with Result ===")
        when (val result = interpreter.executeWithResult("puts 'Testing result types'")) {
            is ExecutionResult.Success -> {
                println("✓ Success! Exit code: ${result.exitCode}, Duration: ${result.durationMs}ms")
            }
            is ExecutionResult.Failure -> {
                println("✗ Failed: ${result.error.message}")
            }
            is ExecutionResult.Timeout -> {
                println("⏱ Timeout after ${result.timeoutSeconds} seconds")
            }
        }
        println()

        // Example 4: Advanced batch builder with named scripts
        println("=== Example 4: Advanced Batch Builder ===")
        val results = interpreter.batch()
            .addScript(
                content = "puts 'Processing user data...'",
                name = "user_processor"
            )
            .addScript(
                content = """
                    data = [1, 2, 3, 4, 5]
                    sum = data.sum
                    puts "Sum: #{sum}"
                """.trimIndent(),
                name = "calculate_sum"
            )
            .addScript(
                content = "puts 'Finalizing...'",
                name = "finalizer"
            )
            .timeout(60)
            .onEachComplete { index, result ->
                val status = if (result.success) "✓" else "✗"
                println("$status Script '${result.name}' (#$index) completed in ${result.durationMs}ms")
            }
            .execute()

        // Example 5: Metrics calculation
        println("\n=== Example 5: Execution Metrics ===")
        val metrics = results.toMetrics()
        println("Total scripts: ${metrics.totalScripts}")
        println("Successful: ${metrics.successCount}")
        println("Failed: ${metrics.failureCount}")
        println("Average duration: ${metrics.averageDurationMs}ms")
        println("Total duration: ${metrics.totalDurationMs}ms")

        // Example 6: Error handling in batch
        println("\n=== Example 6: Batch with Potential Errors ===")
        try {
            val mixedResults = interpreter.batch()
                .addScript("puts 'This works'", name = "success")
                .addScript("raise 'Intentional error'", name = "error")
                .addScript("puts 'This also works'", name = "success2")
                .timeout(10)
                .onEachComplete { index, result ->
                    if (result.success) {
                        println("✓ Script ${result.name} succeeded")
                    } else {
                        println("✗ Script ${result.name} failed with exit code ${result.exitCode}")
                    }
                }
                .execute()

            println("Batch completed. Success rate: ${mixedResults.count { it.success }}/${mixedResults.size}")
        } catch (e: IllegalStateException) {
            println("Batch execution error: ${e.message}")
        }
    }

    println("\n=== All Examples Completed ===")
}
