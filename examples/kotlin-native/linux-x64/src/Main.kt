import com.scorbutics.rubyvm.*
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
    ).use { interpreter ->
        println("✓ Interpreter created successfully!")

        val results = interpreter.batch()
        .addScript("puts 'Hello from Ruby via cinterop!'", name = "greeting")
        .addScript("""
            x = 10
            y = 20
            puts "x + y = #{x + y}"
        """.trimIndent(), name = "calculate")
        .addScript("""
            numbers = [1, 2, 3, 4, 5]
            puts "Numbers: #{numbers.join(', ')}"
            puts "Sum: #{numbers.sum}"
        """.trimIndent(), name = "join_and_sum")
        .timeout(60)
        .onEachComplete { index, result ->
            println("${result.name}: ${if (result.success) "✓" else "✗"} (${result.durationMs}ms)")
        }
        .execute()

    }

    println("\n=== All tests completed successfully! ===")
}
