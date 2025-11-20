package examples

import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.RubyScript

/**
 * Example of using the Ruby VM from JVM/Kotlin
 *
 * To run this example:
 * 1. Build the native library: cmake --build build-linux-native
 * 2. Build the KMP JAR: ./gradlew :ruby-vm-kmp:desktopJar
 * 3. Set java.library.path to the native library location
 * 4. Run with: kotlin -cp kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
 *              -Djava.library.path=build-linux-native/lib JvmExample.kt
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

    // Create interpreter
    println("Creating Ruby interpreter...")
    val interpreter = RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./lib",
        listener = listener
    )
    println("Interpreter created!\n")

    // Create and execute a simple script
    println("Executing Ruby script...")
    val script = RubyScript.fromContent("""
        puts "Hello from Ruby via JVM!"
        puts "Ruby version: #{RUBY_VERSION}"
        puts "2 + 2 = #{2 + 2}"
        puts "Current time: #{Time.now}"
    """.trimIndent())

    interpreter.enqueue(script) { exitCode ->
        println("\nScript completed with exit code: $exitCode")
    }

    // Wait a bit for async execution
    Thread.sleep(2000)

    // Cleanup
    script.destroy()
    interpreter.destroy()
    println("\nCleanup complete!")
}
