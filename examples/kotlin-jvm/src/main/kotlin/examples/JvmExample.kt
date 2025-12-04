package examples

import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.execute
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Example of using the Ruby VM from JVM/Kotlin
 *
 * To run this example:
 * ../../gradlew runExample
 *
 * For more details, see README.md
 */
fun main() {
    println("=== Ruby VM JVM Example ===\n")

    // Create log listener
    val listener = object : LogListener {
        override fun onLog(message: String) {
            println("[Ruby] $message")
        }

        override fun onError(message: String) {
            System.err.println("[Ruby Error] $message")
        }
    }

    // Use CountDownLatch for proper thread synchronization
    // We have 2 scripts, so we need to wait for both
    val latch = CountDownLatch(2)
    
    // Create interpreter
    println("Creating Ruby interpreter...")
    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./lib",
        listener = listener
    ).use { interpreter -> 
        println("Interpreter created!\n")

        // Execute scripts with automatic memory management
        println("Executing script 1...")
        interpreter.execute(
            scriptContent = """
                puts "Hello from Ruby via JVM!"
                puts "Ruby version: #{RUBY_VERSION}"
                puts "2 + 2 = #{2 + 2}"
                puts "Current time: #{Time.now}"
            """.trimIndent(),
            latch = latch
        ) { exitCode ->
            Thread.sleep(5000) // Simulate some work
            println("\nScript 1 completed with exit code: $exitCode")
        }

        println("Executing script 2...")
        interpreter.execute(
            scriptContent = """
                puts "LOL MDR!"
                puts "This is another Ruby script running via JVM."
                puts "3 * 3 = #{3 * 3}"
                puts "Goodbye!"
            """.trimIndent(),
            latch = latch
        ) { exitCode ->
            println("\nScript 2 completed with exit code: $exitCode")
        }

        // Wait for both scripts to complete
        println("Waiting for all scripts to complete...")
        latch.await(30, TimeUnit.SECONDS)
        println("\nAll scripts completed.")

    }

    println("\nCleanup complete!")
}
