# Kotlin/JVM Example

Kotlin example demonstrating idiomatic Kotlin usage of the embedded Ruby VM on the JVM.

## Files

- `JvmExample.kt` - Kotlin/JVM example with idiomatic Kotlin syntax

## Quick Start

### Run with Kotlin Compiler

```bash
# Ensure the KMP JAR is built
cd ../..
./gradlew :ruby-vm-kmp:desktopJar

# Run the example
cd examples/kotlin-jvm
kotlinc -cp ../../kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
    -Djava.library.path=../../lib \
    -script JvmExample.kt
```

### Using Gradle

You can also create a simple Gradle script to run the example:

```kotlin
// build.gradle.kts
plugins {
    kotlin("jvm") version "1.9.0"
}

dependencies {
    implementation(files("../../kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"))
}

tasks.register<JavaExec>("run") {
    classpath = sourceSets.main.get().runtimeClasspath
    mainClass.set("JvmExampleKt")
    systemProperty("java.library.path", "../../lib")
}
```

Then run:
```bash
./gradlew run
```

## What the Example Does

The example demonstrates idiomatic Kotlin features:

### 1. Object Expressions for Callbacks

```kotlin
val listener = object : LogListener {
    override fun onLog(message: String) {
        println("[Ruby] $message")
    }

    override fun onError(message: String) {
        System.err.println("[Ruby Error] $message")
    }
}
```

### 2. Named Parameters

```kotlin
val interpreter = RubyInterpreter.create(
    appPath = ".",
    rubyBaseDir = "./ruby",
    nativeLibsDir = "./lib",
    listener = listener
)
```

### 3. Multi-line String Literals

```kotlin
val script = RubyScript.fromContent("""
    puts "Hello from Ruby via JVM!"
    puts "Ruby version: #{RUBY_VERSION}"
    puts "2 + 2 = #{2 + 2}"
    puts "Current time: #{Time.now}"
""".trimIndent())
```

### 4. Lambda Expressions

```kotlin
interpreter.enqueue(script) { exitCode ->
    println("\nScript completed with exit code: $exitCode")
}
```

## Expected Output

```
=== Ruby VM JVM Example ===

Creating Ruby interpreter...
Interpreter created!

Executing Ruby script...
[Ruby] Hello from Ruby via JVM!
[Ruby] Ruby version: 3.1.0
[Ruby] 2 + 2 = 4
[Ruby] Current time: 2024-12-02 14:30:45 +0000

Script completed with exit code: 0

Cleanup complete!
```

## Differences from Java Example

The Kotlin example showcases:
- **Named parameters** for clarity
- **Object expressions** instead of anonymous classes
- **String templates** and multi-line strings
- **Lambda syntax** for callbacks
- **`trimIndent()`** for clean multi-line Ruby code

Both examples use the same underlying API but demonstrate language-specific idioms.

## Using in Your Kotlin Project

### Gradle Setup

```kotlin
// build.gradle.kts
plugins {
    kotlin("jvm") version "1.9.0"
}

repositories {
    mavenCentral()
    // Add your repository for ruby-vm-kmp
}

dependencies {
    implementation("com.scorbutics.rubyvm:ruby-vm-kmp-desktop:1.0.0-SNAPSHOT")
}

tasks.withType<JavaExec> {
    systemProperty("java.library.path", "path/to/native/libs")
}
```

### Code Example

```kotlin
import com.scorbutics.rubyvm.*

fun main() {
    val interpreter = RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = "./ruby",
        nativeLibsDir = "./lib",
        listener = object : LogListener {
            override fun onLog(message: String) = println(message)
            override fun onError(message: String) = System.err.println(message)
        }
    )

    val script = RubyScript.fromContent("puts 'Hello from Kotlin!'")
    interpreter.enqueue(script) { exitCode ->
        println("Exit code: $exitCode")
    }

    Thread.sleep(1000)  // Wait for async execution

    script.destroy()
    interpreter.destroy()
}
```

## Coroutines Support

For a more Kotlin-idiomatic async approach, you could wrap the callback API:

```kotlin
import kotlinx.coroutines.*
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

suspend fun RubyInterpreter.execute(script: RubyScript): Int =
    suspendCancellableCoroutine { continuation ->
        enqueue(script) { exitCode ->
            continuation.resume(exitCode)
        }
    }

// Usage
runBlocking {
    val exitCode = interpreter.execute(script)
    println("Script finished with code: $exitCode")
}
```

## Troubleshooting

Same troubleshooting steps as the Java example - see `examples/java/README.md`.

## See Also

- **Java Example:** See `examples/java/` for pure Java example
- **Native Example:** See `examples/kotlin-native/` for Kotlin/Native cinterop example
- **API Docs:** See main `README.md` for complete API documentation
