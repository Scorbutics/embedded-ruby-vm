# Kotlin/Native Linux x64 Example

This example demonstrates using the embedded Ruby VM from Kotlin/Native via cinterop on Linux x64.

## Overview

Unlike the JVM examples (which use JNI), this example uses Kotlin/Native's **cinterop** to call the C library directly. This provides:
- Native performance (no JVM overhead)
- Direct C integration
- Smaller binary size
- Platform-specific builds

**Note:** This example was previously part of the production code (`kmp/src/linuxX64Main`) but has been moved to examples as it's demonstration code, not production library code.

## Prerequisites

- Linux x64 system
- Kotlin/Native compiler (via Gradle)
- Native libraries built for Linux x64

## Building and Running

### Option 1: Using Standalone Gradle Build (Recommended)

This directory includes a standalone `build.gradle.kts` that compiles and runs this example independently.

#### Quick Start

```bash
# From this directory (examples/kotlin-native/linux-x64/)
./run.sh
```

The script automatically:
1. Builds native Ruby VM libraries
2. Compiles the Kotlin/Native example
3. Runs the executable

#### Manual Build Steps

If you prefer to run Gradle directly:

```bash
# 1. Build native libraries (from project root)
cd ../../..
./gradlew :ruby-vm-kmp:buildNativeLibsLinux -PtargetArch=x86_64

# 2. Build and run the example
cd examples/kotlin-native/linux-x64
./gradlew runExample
```

#### Build Only

To just build the executable without running:

```bash
./gradlew linkDebugExecutableLinuxX64
```

The executable will be created at:
```
build/bin/linuxX64/debugExecutable/ruby-vm-example.kexe
```

#### Clean Build

```bash
./gradlew clean
```

### Option 2: Using Docker

From the project root:

```bash
# Enter Docker container
./docker-dev.sh shell

# Navigate to example directory
cd examples/kotlin-native/linux-x64

# Build and run
./run.sh
```

### Option 3: Re-enable in KMP Module

Alternatively, this example can be re-enabled as a Kotlin/Native target in the main KMP module:

1. **Uncomment the linuxX64 target** in `kmp/build.gradle.kts`:
   ```kotlin
   linuxX64 {
       binaries {
           executable {
               entryPoint = "main"
               baseName = "ruby-vm-example"
               // ... configuration ...
           }
       }
   }
   ```

2. **Move `Main.kt` back** to `kmp/src/linuxX64Main/kotlin/Main.kt`

3. **Build and run:**
   ```bash
   cd ../../..  # Go to project root
   ./gradlew :ruby-vm-kmp:buildNativeLibsLinux
   ./gradlew :ruby-vm-kmp:runDebugExecutableLinuxX64
   ```

## What the Example Does

This example demonstrates the **high-level Kotlin Native API** that provides the same clean interface as the JVM examples, but runs natively without the JVM overhead.

The example shows how to:

### 1. Imports

```kotlin
import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.RubyScript
import platform.posix.sleep
```

### 2. Creating Log Listener

```kotlin
val listener = object : LogListener {
    override fun onLog(message: String) {
        println("[Ruby] $message")
    }

    override fun onError(message: String) {
        println("[Ruby Error] $message")
    }
}
```

### 3. Creating Interpreter

```kotlin
val interpreter = RubyInterpreter.create(
    appPath = ".",
    rubyBaseDir = "./ruby",
    nativeLibsDir = "./ruby/lib",
    listener = listener
)
```

### 4. Creating and Executing Scripts

```kotlin
val script = RubyScript.fromContent("puts 'Hello from Ruby!'")

interpreter.enqueue(script) { exitCode ->
    println("Script completed with exit code: $exitCode")
    script.destroy()
}
```

### 5. Cleanup

```kotlin
interpreter.destroy()
```

**Note:** This uses the high-level Kotlin Multiplatform API, which internally uses cinterop to call the C library.

The example runs three test scripts:
1. Simple puts statement
2. Ruby variables and math
3. Ruby array operations

Each demonstrates the high-level Kotlin Native API with the same clean interface as JVM examples.

## Expected Output

```
=== Ruby VM Native Test ===

Creating Ruby interpreter...
✓ Interpreter created successfully!

Test 1: Simple puts statement
[Ruby] Hello from Ruby via cinterop!
✓ Script completed with exit code: 0

Test 2: Ruby variables and math
[Ruby] x + y = 30
✓ Math script completed with exit code: 0

Test 3: Ruby arrays
[Ruby] Numbers: 1, 2, 3, 4, 5
[Ruby] Sum: 15
✓ Array script completed with exit code: 0

Cleaning up...

=== All tests completed successfully! ===
```

## Key Differences from JVM Examples

| Aspect | JVM (JNI) | Native (cinterop) |
|--------|-----------|-------------------|
| **API Level** | High-level KMP API | High-level KMP API (same!) |
| **Bridge** | JNI layer | Direct cinterop |
| **Memory** | Garbage collected | Garbage collected (new MM) |
| **Runtime** | JVM required | Native executable |
| **Performance** | Good | Excellent |
| **Binary size** | Large (includes JVM) | Small |
| **Startup time** | Slower (JVM init) | Fast |
| **Platform** | Cross-platform JAR | Platform-specific binary |
| **Usage** | Identical Kotlin API | Identical Kotlin API |
| **Implementation** | JNI calls under the hood | cinterop calls under the hood |

## Architecture

### Source Set Structure

The standalone build uses the shared KMP code from the main project:

```
build.gradle.kts configures three source sets:
├── commonMain (from kmp/src/commonMain/)
│   ├── LogListener.kt (interface)
│   ├── RubyInterpreter.kt (expect class)
│   └── RubyScript.kt (expect class)
├── nativeMain (from kmp/src/nativeMain/)
│   ├── RubyInterpreter.native.kt (actual implementation using cinterop)
│   └── RubyScript.native.kt (actual implementation)
└── linuxX64Main (from src/)
    └── Main.kt (example code)
```

This architecture allows the example to use the same high-level API as JVM examples while benefiting from native performance.

### Cinterop Definition

The cinterop definition is in `kmp/src/nativeInterop/cinterop/ruby_vm.def`:

```
headers = ruby-interpreter.h logging.h
headerFilter = ruby-interpreter.h logging.h
package = com.scorbutics.rubyvm.native

compilerOpts.linux = -I../../../core/ruby-vm -I../../../core/logging
linkerOpts.linux = -L../../../libs/linux_x64 -lruby-vm -llogging -lassets -lminizip
```

This maps the C API to Kotlin/Native, which is used internally by the `actual` implementations in nativeMain.

## Building Native Libraries

Before running this example, ensure native libraries are built:

```bash
./gradlew :ruby-vm-kmp:buildNativeLibsLinux -PtargetArch=x86_64
```

This creates:
- `kmp/libs/linux_x64/libruby-vm.a`
- `kmp/libs/linux_x64/liblogging.a`
- `kmp/libs/linux_x64/libassets.a`
- etc.

## Project Structure

```
examples/kotlin-native/linux-x64/
├── src/
│   └── Main.kt              # Example source code
├── build.gradle.kts         # Standalone Gradle build with source set config
├── settings.gradle.kts      # Gradle settings
├── gradle.properties        # Gradle configuration
├── run.sh                   # Helper script to build and run
├── .gitignore              # Git ignore patterns
└── README.md               # This file
```

The standalone build automatically:
- Includes KMP source sets: commonMain → nativeMain → linuxX64Main
- Uses the cinterop definition from `kmp/src/nativeInterop/cinterop/ruby_vm.def`
- Links against libraries in `kmp/libs/linux_x64/`
- Links against Ruby in `core/external/lib/x86_64-linux-linux/`
- Sets up proper RPATH for runtime library loading

## Platform Support

Currently supports:
- ✅ Linux x64

Could be extended to:
- 🔄 Linux ARM64 (requires Kotlin/Native support)
- 🔄 macOS x64/ARM64
- 🔄 iOS (simulator and device)

## See Also

- **JVM Examples:** See `examples/java/` and `examples/kotlin-jvm/` for JNI-based examples
- **Cinterop Guide:** Kotlin/Native cinterop documentation
- **API Docs:** See main `README.md` for C API reference
