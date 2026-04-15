# Embedded Ruby VM

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Kotlin](https://img.shields.io/badge/Kotlin-Multiplatform-blue.svg)](https://kotlinlang.org/docs/multiplatform.html)
[![Ruby](https://img.shields.io/badge/Ruby-3.1.0-red.svg)](https://www.ruby-lang.org/)

A cross-platform C library that embeds a full Ruby interpreter for use in native applications, Android, iOS, and JVM applications. Execute Ruby scripts from your Kotlin, Java, or C code with a complete Ruby runtime environment and standard library embedded directly into your binary.

## ✨ Features

- 🚀 **Full Ruby 3.1.0 Runtime** - Complete Ruby interpreter with standard library
- 📦 **Single Binary Distribution** - No external Ruby installation required
- 🌍 **Cross-Platform** - Android, iOS, macOS, Linux, Windows, JVM Desktop
- 🔧 **Kotlin Multiplatform API** - Unified API across all platforms
- ⚡ **Native Performance** - Direct C integration via JNI and cinterop
- 🧵 **Thread-Safe** - Isolated Ruby VM with async script execution
- 📝 **Comprehensive Logging** - Capture Ruby stdout/stderr via callbacks
- 🎯 **Zero Dependencies** - Self-contained with embedded assets
- ✨ **Ergonomic APIs** - Batch execution, builder pattern, structured results
- 📊 **Execution Metrics** - Track duration, success rates, and performance

## 🏗️ Architecture

The project uses a **three-layer architecture** with hybrid static/dynamic library loading for maximum platform compatibility:

```
┌─────────────────────────────────────────────────┐
│  Kotlin Multiplatform (KMP) - Unified API      │
│  - commonMain: Shared interfaces                │
│  - androidMain/jvmMain: JNI implementations     │
│  - nativeMain: cinterop implementations         │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  Native Bridge Layer                            │
│  - JNI: For Android & JVM Desktop               │
│  - cinterop + dlopen: For Native platforms      │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  C Core Library - Embedded Ruby VM              │
│  - libembedded-ruby.so (dynamic)                │
│  - libassets.a (static) - Asset extraction      │
│  - Ruby VM integration, logging, threading      │
└─────────────────────────────────────────────────┘
```

### Library Loading Architecture

The project supports both **static** and **dynamic** linking approaches through a unified API:

**Dynamic Loading (Kotlin/Native, JNI)**:
- `libembedded-ruby.so` is loaded at runtime via `dlopen()`
- Ruby runtime (`libruby.so`) is extracted by the asset system at first run
- Dependency preloading via `.deps` file ensures all shared libraries are available
- Uses `--whole-archive` linker flag to export symbols from static libraries

**Static Loading (Optional, via `-DRUBY_STATIC`)**:
- Function pointers assigned directly to statically-linked functions
- Same `ruby_api_load()` function signature for both approaches
- Transparent switching via compile-time define

**Key Design Benefits**:
- Ruby runtime deployed at runtime, not hardcoded at build time
- Single unified API regardless of linking approach
- Clean separation between build-time and runtime dependencies

### Supported Platforms

| Platform | Architecture | Bridge | Status |
|----------|-------------|--------|--------|
| **Android** | arm64-v8a, x86_64 | JNI | ✅ Supported |
| **JVM Desktop** | x86_64 | JNI | ✅ Supported |
| **Linux Native** | x86_64 | cinterop | ✅ Supported |
| **iOS** | arm64, simulator | cinterop | ✅ Supported |
| **macOS** | arm64, x86_64 | cinterop | ✅ Supported |

## 🚀 Quick Start

### Prerequisites

- **Java Development Kit (JDK)** 11 or higher
- **CMake** 3.19.2 or higher
- **Gradle** (wrapper included)
- **Android NDK** (for Android builds only)

> WARNING: Please note that ideally (and while building on host directly is supported), ALL of your commands must be executed INSIDE the docker container!
>
> That means you will have to prefix every exposed command down here with `docker exec <container_name>` (the container name should be `embedded-ruby-vm-dev`).
>
> The docker container stack mounted using docker-compose.yml is using a named volume and not a bind mount. In order to sync the sources, each time you are doing a source code modification, you have to remove the `source-sync-in` (`docker-compose run --rm source-sync-in`) container which will trigger a resync next build.
> Alternatively, you can also use the `docker-dev.sh` script for convenient usage.

### Build Everything

```bash
# Build for your current architecture
./gradlew build

# Build for specific architecture
./gradlew build -PtargetArch=x86_64
./gradlew build -PtargetArch=arm64
./gradlew build -PtargetArch=all

# Debug build
./gradlew build -PbuildType=Debug
```

That's it! Gradle automatically:
1. Detects your OS and architecture
2. Runs CMake to compile native libraries
3. Compiles Kotlin code
4. Packages everything into distributable artifacts

### Run Examples

```bash
# Run the improved API example (recommended)
cd examples/kotlin-jvm
../../gradlew runExample

# Run original example
../../gradlew runExample -PexampleClass=JvmExample

# Run all examples
../../gradlew runAllExamples
```

## 📖 Usage

### Kotlin Multiplatform (Recommended)

#### Quick Start - Simple Batch Execution

```kotlin
import com.scorbutics.rubyvm.*

// Create log listener
val listener = object : LogListener {
    override fun onLog(message: String) = println("[Ruby] $message")
    override fun onError(message: String) = System.err.println("[Ruby Error] $message")
}

// Create interpreter with automatic cleanup
RubyInterpreter.create(
    appPath = ".",
    rubyBaseDir = "./ruby",
    nativeLibsDir = "./lib",
    listener = listener
).use { interpreter ->

    // Execute multiple scripts - no manual synchronization needed!
    val exitCodes = interpreter.executeBatch(
        scripts = listOf(
            "puts 'Hello from Ruby!'",
            "puts 'Ruby version: #{RUBY_VERSION}'",
            "puts '2 + 2 = #{2 + 2}'"
        ),
        timeoutSeconds = 30
    )

    println("All scripts completed: $exitCodes")
}
```

#### Advanced Usage - Builder Pattern with Named Scripts

```kotlin
RubyInterpreter.create(...).use { interpreter ->
    val results = interpreter.batch()
        .addScript("puts 'Initializing...'", name = "init")
        .addScript("data = [1,2,3,4,5]; puts data.sum", name = "calculate")
        .addScript("puts 'Done!'", name = "cleanup")
        .timeout(60)
        .onEachComplete { index, result ->
            println("${result.name}: ${if (result.success) "✓" else "✗"} (${result.durationMs}ms)")
        }
        .execute()

    // Get metrics
    val metrics = results.toMetrics()
    println("Success rate: ${metrics.successCount}/${metrics.totalScripts}")
}
```

#### Synchronous Execution (Blocking)

```kotlin
RubyInterpreter.create(...).use { interpreter ->
    val exitCode = interpreter.executeSync(
        scriptContent = "puts 'This blocks until completion'",
        timeoutSeconds = 10
    )
    println("Exit code: $exitCode")
}
```

#### Low-Level API (For Advanced Use Cases)

```kotlin
import com.scorbutics.rubyvm.RubyScript
import java.util.concurrent.CountDownLatch

// For custom synchronization with external systems
val latch = CountDownLatch(2)  // Ruby scripts + external tasks

val script = RubyScript.fromContent("puts 'Hello from Ruby!'")
interpreter.enqueue(script) { exitCode ->
    println("Script completed: $exitCode")
    script.close()
    latch.countDown()
}

externalSystem.doWork { latch.countDown() }
latch.await()  // Wait for both
```

### C API

```c
#include "ruby-interpreter.h"

// Create interpreter
RubyInterpreter* interpreter = ruby_interpreter_create(
    ".",                    // Working directory
    "./ruby",              // Ruby stdlib location
    "./lib",               // Native extensions path
    log_listener           // Logging callbacks
);

// Create script
RubyScript* script = ruby_script_create_from_content(
    "puts 'Hello from Ruby!'",
    strlen("puts 'Hello from Ruby!'")
);

// Execute script
ruby_interpreter_enqueue(interpreter, script, completion_callback);

// Cleanup
ruby_script_destroy(script);
ruby_interpreter_destroy(interpreter);
```

### Java/JNI API

```java
import com.scorbutics.rubyvm.RubyVMNative;
import com.scorbutics.rubyvm.LogListener;

// Create interpreter
long interpreterPtr = RubyVMNative.createInterpreter(
    ".",
    "./ruby",
    "./lib",
    new LogListener() {
        public void onLog(String message) {
            System.out.println("[Ruby] " + message);
        }
        public void onError(String message) {
            System.err.println("[Ruby Error] " + message);
        }
    }
);

// Create and execute script
long scriptPtr = RubyVMNative.createScript("puts 'Hello from Ruby!'");
RubyVMNative.enqueueScript(interpreterPtr, scriptPtr, exitCode -> {
    System.out.println("Script completed: " + exitCode);
});
```

## 📂 Project Structure

```
embedded-ruby-vm/
├── core/                      # C library core
│   ├── ruby-vm/              # Ruby VM wrapper and communication
│   ├── assets/               # Embedded Ruby stdlib and scripts
│   ├── logging/              # Logging system
│   └── external/             # External dependencies (Ruby libs)
├── jni/                      # JNI bindings for Android/JVM
│   ├── android/              # Android-specific logging
│   └── *.c/h                 # JNI bridge implementation
├── kmp/                      # Kotlin Multiplatform module
│   ├── src/
│   │   ├── commonMain/       # Shared API definitions
│   │   ├── androidMain/      # Android implementation (JNI)
│   │   ├── desktopMain/      # JVM Desktop implementation (JNI)
│   │   ├── nativeMain/       # iOS/macOS/Linux (cinterop)
│   │   └── desktopTest/      # KMP unit tests
│   └── build.gradle.kts      # KMP build configuration
├── tests/                    # Test suites (organized by technology)
│   ├── native/               # Native C tests
│   │   ├── core/             # Core library tests
│   │   ├── jni/              # JNI layer tests
│   │   └── jni-android/      # Android logging tests
│   └── README.md             # Test documentation
├── examples/                 # Usage examples (organized by language)
│   ├── java/                 # Java examples
│   │   ├── SimpleJavaExample.java
│   │   └── run-java-example.sh
│   ├── kotlin-jvm/           # Kotlin/JVM examples
│   │   ├── JvmExample.kt            # Original example (manual latch)
│   │   ├── ImprovedApiExample.kt    # Improved API example
│   │   └── build.gradle.kts         # Build configuration
│   ├── kotlin-native/        # Kotlin/Native examples
│   │   └── linux-x64/        # Linux x64 cinterop example
│   └── README.md             # Examples documentation
├── CMakeLists.txt            # Root CMake configuration
├── build.gradle.kts          # Root Gradle configuration
├── CLAUDE.md                 # Detailed technical documentation
└── README.md                 # This file
```

## 🔧 Platform-Specific Builds

### Android

```bash
# Quick iteration (arm64 only)
./gradlew :ruby-vm-kmp:assembleDebug -PtargetArch=arm64

# Production build (all ABIs)
./gradlew :ruby-vm-kmp:assembleRelease -PtargetArch=all
```

### Desktop JVM

```bash
# Build JAR with embedded native library
./gradlew :ruby-vm-kmp:desktopJar

# For specific architecture
./gradlew :ruby-vm-kmp:desktopJar -PtargetArch=x86_64
```

### Linux Native (cinterop - without JVM)

```bash
# Build native executable (no JVM required)
./gradlew :ruby-vm-kmp:linuxX64MainBinaries

# Build native C library for Linux
./gradlew buildNativeLibsLinux
```

**Note**: This target uses Kotlin/Native with cinterop to directly call C functions, providing a JVM-free native Linux binary.

### iOS (when enabled)

```bash
# Build for device
./gradlew buildNativeLibsIOS -PtargetArch=arm64

# Build for simulator
./gradlew buildNativeLibsIOS -PtargetArch=x86_64
```

### Native Libraries Only

```bash
# All platforms
./gradlew buildAllNativeLibs

# Specific platform
./gradlew buildNativeLibsAndroid
./gradlew buildNativeLibsDesktop
./gradlew buildNativeLibsIOS
./gradlew buildNativeLibsMacOS
./gradlew buildNativeLibsLinux
```

## 🧪 Testing

```bash
# Build and run all tests
./gradlew build

# Run specific test levels
cd build
./bin/test_core                     # Core library tests
./bin/test_jni                      # JNI layer tests
./bin/test_jni_android_log          # Android logging tests
```

## 🎯 Key Features Explained

### Embedded Assets

The Ruby standard library (11MB) is packaged as a zip file and embedded directly into the binary using CMake's object file generation. This means:
- ✅ No external Ruby installation needed
- ✅ Single binary distribution
- ✅ Consistent stdlib version across platforms
- ✅ Platform-specific builds (aarch64, x86_64)

### Communication Architecture

The Ruby VM runs in a dedicated thread and communicates via Unix domain sockets:
- **Protocol**: `<length>\n<script_content>` → Ruby executes → `<exit_code>\n`
- **Isolation**: Ruby crashes don't affect the main application
- **Async Execution**: Scripts are enqueued and executed sequentially
- **Output Capture**: All Ruby stdout/stderr is captured and forwarded to callbacks

### Platform-Agnostic Logging

The JNI layer uses a **weak symbol pattern** for pluggable logging:
- Default: No-op logging (zero overhead)
- Android: Automatically uses `__android_log_write` (logcat)
- Custom: Provide your own `jni_log_impl()` implementation

### Threading Model

- **Main VM Thread**: Runs the Ruby interpreter
- **Log Reader Thread**: Reads stdout/stderr from Ruby
- **Script Execution**: Asynchronous with completion callbacks

## 📚 Documentation

- **[CLAUDE.md](CLAUDE.md)** - Comprehensive technical documentation
  - Architecture deep-dive
  - Low-level API reference (C, JNI, Kotlin)
  - Build system details
  - Development guidelines
  - Troubleshooting

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

### Development Setup

1. Clone the repository
2. Install prerequisites (JDK, CMake, Gradle)
3. Build the project: `./gradlew build`
4. Run tests: `./gradlew test`

### Adding Features

**For low-level Ruby VM features** (requires C changes):
1. Extend C API in `core/ruby-vm/ruby-interpreter.h`
2. Implement in `core/ruby-vm/*.c`
3. Update JNI bindings in `jni/ruby_vm_jni.c` (for JVM platforms)
4. Update Kotlin bindings in `kmp/src/`
5. Add tests in `tests/`

**For Kotlin convenience APIs** (no C changes needed):
1. Add common interface in `kmp/src/commonMain/kotlin/`
2. Implement for JVM in `kmp/src/jvmMain/kotlin/`
3. Implement for Native in `kmp/src/nativeMain/kotlin/`
4. Add examples in `examples/kotlin-jvm/`


## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Ruby programming language and community
- Kotlin Multiplatform team
- minizip-ng for zip handling

## 📞 Support

For issues, questions, or contributions, please open an issue on the GitHub repository.

