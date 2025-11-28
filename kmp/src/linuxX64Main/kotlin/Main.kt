import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.RubyScript

import platform.posix.sleep

import kotlinx.cinterop.ExperimentalForeignApi

/**
 * Simple test application for Linux x64 native Ruby VM.
 *
 * Run with: ./gradlew :ruby-vm-kmp:runDebugExecutableLinuxX64
 */
@OptIn(ExperimentalForeignApi::class)
fun main() {
    println("=== Ruby VM Native Test (Linux x64) ===")
    println()

    val sleepTime = 5u

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
    val interpreter = RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./ruby/lib",
        listener = listener
    )
    println("✓ Interpreter created successfully!")
    println()

    // Test 1: Simple output
    println("Test 1: Simple puts statement")
    val script1 = RubyScript.fromContent("puts 'Hello from Ruby via cinterop!'")
    interpreter.enqueue(script1) { exitCode ->
        println("✓ Script completed with exit code: $exitCode")
    }
    sleep(sleepTime) // Give it time to execute

    // Test 2: Ruby code with variables
    println("\nTest 2: Ruby variables and math")
    val script2 = RubyScript.fromContent("""
        x = 10
        y = 20
        puts "x + y = #{x + y}"
    """.trimIndent())
    interpreter.enqueue(script2) { exitCode ->
        println("✓ Math script completed with exit code: $exitCode")
    }
    sleep(sleepTime)

    // Test 3: Ruby array operations
    println("\nTest 3: Ruby arrays")
    val script3 = RubyScript.fromContent("""
        numbers = [1, 2, 3, 4, 5]
        puts "Numbers: #{numbers.join(', ')}"
        puts "Sum: #{numbers.sum}"
    """.trimIndent())
    interpreter.enqueue(script3) { exitCode ->
        println("✓ Array script completed with exit code: $exitCode")
    }
    sleep(sleepTime * 10u)

    // Cleanup
    println("\nCleaning up...")
    script1.destroy()
    script2.destroy()
    script3.destroy()
    interpreter.destroy()

    println("\n=== All tests completed successfully! ===")
}
