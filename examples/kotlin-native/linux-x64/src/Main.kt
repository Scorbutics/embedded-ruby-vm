import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.RubyScript

import kotlinx.cinterop.ExperimentalForeignApi

/**
 * Simple test application for native Ruby VM.
 */
@OptIn(ExperimentalForeignApi::class)
fun main() {
    println("=== Ruby VM Native Test ===")
    println()

    var step = 0u

    // Create a log listener
    val listener = object : LogListener {
        override fun onLog(message: String) {
            println("[Ruby] $message")
        }

        override fun onError(message: String) {
            kotlin.io.println("[Ruby Error] $message")
        }
    }

    // Create interpreter
    println("Creating Ruby interpreter...")
    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./ruby/lib",
        listener = listener
    ).use { 
        interpreter -> 
        println("✓ Interpreter created successfully!")
        println()

        // Test 1: Simple output
        println("Test 1: Simple puts statement")
        RubyScript.fromContent("puts 'Hello from Ruby via cinterop!'").use { 
            script1 ->
            interpreter.enqueue(script1) { exitCode ->
                println("✓ Script completed with exit code: $exitCode")
                step++
            }
            do while(step < 1u)
        }

        // Test 2: Ruby code with variables
        println("\nTest 2: Ruby variables and math")
        RubyScript.fromContent("""
            x = 10
            y = 20
            puts "x + y = #{x + y}"
        """.trimIndent()).use { 
            script2 ->
            interpreter.enqueue(script2) { exitCode ->
                println("✓ Math script completed with exit code: $exitCode")
                step++
            }
            do while(step < 2u)
        }

        // Test 3: Ruby array operations
        println("\nTest 3: Ruby arrays")
        RubyScript.fromContent("""
            numbers = [1, 2, 3, 4, 5]
            puts "Numbers: #{numbers.join(', ')}"
            puts "Sum: #{numbers.sum}"
        """.trimIndent()).use { 
            script3 ->
            interpreter.enqueue(script3) { exitCode ->
                println("✓ Array script completed with exit code: $exitCode")
                step++
            }
            do while(step < 3u)
        }

    }

    println("\n=== All tests completed successfully! ===")
}
