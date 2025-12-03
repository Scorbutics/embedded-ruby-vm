package examples

import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.RubyScript
import java.util.concurrent.atomic.AtomicInteger

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

    // Use AtomicInteger for thread-safe counter with proper memory barriers
    val scriptCount = AtomicInteger(0)
    // Create interpreter
    println("Creating Ruby interpreter...")
    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./lib",
        listener = listener
    ).use { interpreter -> 
        println("Interpreter created!\n")

        // Create and execute a simple script
        println("Executing Ruby script...")
        RubyScript.fromContent("""
            puts "Hello from Ruby via JVM!"
            puts "Ruby version: #{RUBY_VERSION}"
            puts "2 + 2 = #{2 + 2}"
            puts "Current time: #{Time.now}"
        """.trimIndent()).use { script ->

            interpreter.enqueue(script) { exitCode ->
                println("\nScript completed with exit code: $exitCode")
                scriptCount.incrementAndGet()
            }

            /*RubyScript.fromContent("""
                puts "LOL MDR!"
                puts "This is another Ruby script running via JVM."
                puts "3 * 3 = #{3 * 3}"
                puts "Goodbye!"
            """.trimIndent()).use { script2 ->

                interpreter.enqueue(script2) { exitCode ->
                    println("\nScript completed with exit code: $exitCode")
                    scriptCount.incrementAndGet()
                }
            }*/

            // Wait for scripts to complete
            // AtomicInteger provides memory barriers, so busy-wait is now safe
            while (scriptCount.get() < 2) {
                // Empty loop - atomic operations provide proper memory synchronization
            }

        }

    }

    println("\nCleanup complete!")
}
