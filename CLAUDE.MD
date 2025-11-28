# Embedded Ruby VM

## Project Overview

This is a C library that embeds a full Ruby interpreter for use in native applications and Android apps. It provides a complete Ruby runtime environment with the standard library embedded directly into the binary, allowing Ruby script execution without requiring an external Ruby installation.

## Architecture

### Multi-Layer Architecture

This project uses a **three-layer architecture** for maximum platform compatibility:

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
│  - cinterop: For iOS & Native Desktop           │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  C Core Library - Embedded Ruby VM              │
│  - Cross-platform Ruby interpreter              │
│  - Asset management, logging, threading         │
└─────────────────────────────────────────────────┘
```

**Supported Platforms:**
- Android (via JNI)
- JVM Desktop (via JNI)
- iOS (arm64 + simulator via cinterop)
- macOS (arm64 + x64 via cinterop)
- Linux (x64 via cinterop)

### Core Components

1. **Ruby VM Wrapper** (`core/ruby-vm/`)
   - Manages a Ruby interpreter instance running in a dedicated thread
   - Provides C API for creating interpreters and executing scripts
   - Uses socket pairs for bidirectional communication between C and Ruby
   - Key files:
     - `ruby-vm.c/h`: Main VM lifecycle management
     - `ruby-interpreter.c/h`: High-level interpreter API
     - `exec-main-vm.c`: VM initialization and execution logic
     - `ruby-script.c/h`: Script representation and management

2. **Communication Layer** (`core/ruby-vm/`)
   - FIFO-style command channel using Unix domain sockets
   - Protocol: `<length>\n<script_content>` from C → Ruby executes → `<exit_code>\n` back to C
   - Asynchronous script execution with completion callbacks
   - Files: `ruby-comm-channel.c/h`

3. **Embedded Assets** (`core/assets/`)
   - Ruby standard library packaged as zip (11MB)
   - FIFO interpreter script (`fifo_interpreter.rb`) for executing commands
   - Assets compiled into object files and linked into binary
   - Platform-specific builds for different architectures (aarch64, x86_64)
   - Files: `embed_binary.cmake`, `install.c/h`, `use_direct_memory.c/h`

4. **Logging System** (`core/logging/`)
   - Callback-based logging with separate stdout/stderr handlers
   - Thread-safe log reading from Ruby subprocess
   - Files: `logging.c/h`

5. **Android JNI Bindings** (`jni/`)
   - JNI wrapper for Android/Kotlin integration
   - Java package: `com.scorbutics.rubyvm.RubyVMNative`
   - Provides Java-friendly API for interpreter lifecycle and script execution
   - **Platform-agnostic logging**: Uses weak symbols for pluggable logging implementations
   - Files:
     - `ruby_vm_jni.c/h`: Main JNI bridge implementation
     - `jni_ruby_info.c/h`: Ruby version info
     - `get_activity_parameters.c/h`: Android activity integration
     - `jni_logging.c/h`: Platform-agnostic logging interface (weak symbols)
     - `android/`: Android-specific logging module (uses liblog)

### Threading Model

- **Main VM Thread**: Runs the Ruby interpreter subprocess/process
- **Log Reader Thread**: Reads stdout/stderr from Ruby and forwards to callbacks
- **Script Execution**: Scripts are enqueued and executed sequentially in the VM

### Build System

**Two-tier build system:**

1. **Gradle** (top-level) - Kotlin Multiplatform orchestration
   - Coordinates builds for all platforms
   - Invokes CMake for each target platform
   - Manages Kotlin compilation and packaging
   - Generates XCFramework for iOS distribution

2. **CMake** (native layer) - C library compilation
   - Minimum version: 3.19.2
   - Build Options:
     - `BUILD_JNI`: Enable JNI bindings (default: OFF)
     - `BUILD_JNI_ANDROID_LOG`: Enable Android logging module (default: OFF)
     - `BUILD_TESTS`: Build test suite (default: ON)
   - Host-specific: Platform triplets (e.g., `aarch64-linux-android`)
   - External Dependencies:
     - Ruby shared library in `core/external/lib/{host}/`
     - minizip-ng (fetched automatically)

**Building with Gradle (Recommended):**
```bash
# Build all platforms
./gradlew build

# Build specific platform
./gradlew :ruby-vm-kmp:desktopJar              # JVM Desktop
./gradlew :ruby-vm-kmp:assembleRelease     # Android
./gradlew :ruby-vm-kmp:linkDebugFrameworkIosArm64  # iOS

# Run CMake tasks to build native libraries
./gradlew buildNativeLibsIOS
./gradlew buildNativeLibsMacOS
./gradlew buildNativeLibsLinux
```

**Building with CMake only (C library):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Key APIs

### C API

```c
// Create interpreter
RubyInterpreter* ruby_interpreter_create(
    const char* application_path,      // Working directory
    const char* ruby_base_directory,   // Ruby stdlib location
    const char* native_libs_location,  // Native extensions path
    LogListener listener               // Logging callbacks
);

// Execute script
int ruby_interpreter_enqueue(
    RubyInterpreter* interpreter,
    RubyScript* script,
    RubyCompletionTask on_complete    // Completion callback
);

// Create script from content
RubyScript* ruby_script_create_from_content(
    const char* content,
    size_t length
);
```

### Kotlin Multiplatform API (Unified API for all platforms)

```kotlin
// Package: com.scorbutics.rubyvm

// Log listener interface
interface LogListener {
    fun onLog(message: String)
    fun onError(message: String)
}

// Create interpreter (works on all platforms)
val interpreter = RubyInterpreter.create(
    appPath = "/path/to/app",
    rubyBaseDir = "/path/to/ruby",
    nativeLibsDir = "/path/to/native/libs",
    listener = object : LogListener {
        override fun onLog(message: String) {
            println("Ruby: $message")
        }
        override fun onError(message: String) {
            System.err.println("Ruby Error: $message")
        }
    }
)

// Create and execute script
val script = RubyScript.fromContent("puts 'Hello from Ruby!'")
interpreter.enqueue(script) { exitCode ->
    println("Script completed with exit code: $exitCode")
}

// Cleanup
script.destroy()
interpreter.destroy()
```

### JNI API (Low-level, used internally by KMP)

```java
// Package: com.scorbutics.rubyvm
// This is used internally by androidMain/jvmMain implementations
class RubyVMNative {
    static native long createInterpreter(String appPath, String rubyBaseDir,
                                         String nativeLibsLocation, LogListener listener);
    static native void destroyInterpreter(long interpreterPtr);
    static native long createScript(String content);
    static native void enqueueScript(long interpreterPtr, long scriptPtr,
                                     CompletionCallback callback);
}
```

### JNI Logging Architecture

The JNI layer uses a **weak symbol pattern** for platform-agnostic logging:

**How it works:**
1. `jni_logging.c` defines a weak symbol `jni_log_impl()` (default: no-op)
2. Platform-specific modules override this with a strong symbol
3. Android builds automatically link `jni/android/` module (uses `__android_log_write`)
4. Non-Android platforms get silent logging (or can provide custom implementation)

**For Android Users:**
- **Automatic**: Android logging is automatically enabled when building with `BUILD_JNI=ON`
- Logs go to logcat via `__android_log_write`
- No setup required!

**For Custom Platform Logging:**

Create your own implementation of `jni_log_impl()`:

```c
// custom_logging.c
#include "jni/jni_logging.h"
#include <stdio.h>

// Provide strong symbol to override weak default
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message) {
    const char* level = priority >= JNI_LOG_ERROR ? "ERROR" :
                        priority >= JNI_LOG_WARN ? "WARN" : "INFO";
    fprintf(stderr, "[%s] %s: %s\n", level, tag, message);
}
```

Then link `custom_logging.o` with your application.

**Architecture Benefits:**
- ✅ **Zero runtime overhead** for disabled logging
- ✅ **No Java/JNI callbacks** needed
- ✅ **Automatic platform detection** via CMake
- ✅ **Simple to extend** for new platforms
- ✅ **Compile-time linking** - no registration needed

## Development Guidelines

### Working with the Codebase

1. **Reading Ruby Output**: All Ruby `puts`/`print` statements are captured via the communication channel and forwarded to the LogListener callbacks

2. **Script Execution Flow**:
   - Script is created with content
   - Enqueued to interpreter
   - Sent to Ruby via socket: `<length>\n<content>`
   - Ruby's fifo_interpreter.rb receives and executes
   - Exit code returned via socket
   - Completion callback invoked

3. **Adding Features**:
   - Extend C API in `ruby-interpreter.h`
   - Update JNI bindings if needed for Android
   - Consider thread safety (use mutexes for shared state)

4. **Testing**:
   - Main test: `tests/test.c`
   - Runs a simple Ruby script and verifies output
   - Build with `BUILD_TESTS=ON` (default)

### Common Tasks

**Building with Kotlin Multiplatform (Recommended)**:
```bash
# First time setup - download Gradle wrapper
./gradlew wrapper

# Build all platforms
./gradlew build

# Build native libraries for each platform
./gradlew buildNativeLibsAndroid buildNativeLibsIOS buildNativeLibsMacOS buildNativeLibsLinux

# Clean build
./gradlew clean
```

**Building C Library Only (CMake)**:
```bash
# Basic build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With Android JNI
cmake -B build -DBUILD_JNI=ON
cmake --build build

# With Android logging
cmake -B build -DBUILD_JNI=ON -DBUILD_JNI_ANDROID_LOG=ON
cmake --build build
```

**Running Tests**:
```bash
cd build
ctest

# Or run specific test levels
./bin/test_core                     # Core library tests
./bin/test_jni                      # JNI layer tests (if built)
./bin/test_jni_android_log          # Android logging tests (if built)
```

### Architecture Decisions

1. **Why Kotlin Multiplatform?**
   - **Single codebase**: Write API once, works everywhere
   - **Native performance**: No bridge overhead for native platforms (cinterop)
   - **Minimal duplication**: Only platform-specific implementations differ
   - **Type safety**: Compile-time errors instead of runtime crashes

2. **Why Two Bridge Technologies (JNI + cinterop)?**
   - **JNI for Android/JVM**: Reuses existing, battle-tested JNI layer
   - **cinterop for iOS/Native**: Direct C calls, better performance, no JVM overhead
   - **Best of both worlds**: Right tool for each platform

3. **Why Socket Communication?**
   - Isolates Ruby VM in separate process/thread
   - Prevents crashes in Ruby from affecting main application
   - Clean separation of concerns

4. **Why Embed Assets?**
   - Single-binary deployment
   - No external Ruby installation required
   - Consistent stdlib version across platforms

5. **Threading vs Process**
   - Currently uses threads with socket pairs
   - Could be adapted to use separate processes for better isolation

## Files of Interest

### Kotlin Multiplatform Layer
- `kmp/build.gradle.kts`: KMP module configuration with platform targets
- `kmp/src/commonMain/kotlin/`: Shared Kotlin API (expect declarations)
  - `RubyInterpreter.kt`: Main interpreter interface
  - `RubyScript.kt`: Script representation
  - `LogListener.kt`: Logging callback interface
- `kmp/src/androidMain/kotlin/`: Android implementations (JNI-based)
  - `RubyInterpreter.android.kt`
  - `RubyScript.android.kt`
- `kmp/src/jvmMain/kotlin/`: JVM Desktop implementations (JNI-based)
  - `RubyInterpreter.jvm.kt`
  - `RubyScript.jvm.kt`
- `kmp/src/nativeMain/kotlin/`: Native implementations (cinterop-based)
  - `RubyInterpreter.native.kt`: iOS/macOS/Linux implementation
  - `RubyScript.native.kt`
- `kmp/src/nativeInterop/cinterop/ruby_vm.def`: cinterop configuration

### Build System
- `settings.gradle.kts`: Gradle project configuration
- `build.gradle.kts`: Root Gradle build file
- `gradle.properties`: Gradle settings
- `gradle/libs.versions.toml`: Version catalog for dependencies
- `core/assets/embed_binary.cmake`: Asset embedding logic

### Core C Library
- `core/ruby-vm/exec-main-vm.c`: Ruby VM initialization and main execution loop
- `core/assets/files/fifo_interpreter.rb`: Ruby-side script executor
- `jni/ruby_vm_jni.c`: JNI bridge implementation
- `jni/jni_logging.c/h`: Weak symbol logging interface
- `jni/android/android_logging.c`: Android logging implementation (strong symbol)

### Tests
- `tests/core/test_core.c`: Core library tests
- `tests/jni/test_jni.c`: JNI layer tests (weak symbol verification)
- `tests/jni-android/test_jni_android_log.c`: Android logging tests

## Platform Support

### Kotlin Multiplatform Targets
- **Android**: API 21+ (armeabi-v7a, arm64-v8a, x86, x86_64)
- **JVM Desktop**: Java 8+ (Windows, macOS, Linux)
- **iOS**: 12.0+ (arm64 device, x64/arm64 simulator) - XCFramework
- **macOS**: 10.13+ (arm64, x64)
- **Linux**: x64

### Native C Library Support
- Linux (Termux/Android)
- Android (via JNI)
- Architecture: aarch64, x86_64
- Host format: `{arch}-{platform}-{os}` (e.g., `aarch64-linux-android`)

## External Dependencies

- **Ruby**: Shared library required in `core/external/lib/{host}/libruby.so`
- **minizip-ng**: Fetched automatically by CMake for zip handling
- **pthreads**: For threading support
- **zlib**: Required for JNI builds (zip handling)

**Platform-Specific:**
- **Android**: `liblog` (automatically linked by `jni/android/` module when `BUILD_JNI_ANDROID_LOG=ON`)
- **Other platforms**: No platform-specific dependencies (uses no-op logging by default)

## Troubleshooting

- **Logging**: Check `ruby_vm_test.log` for Ruby output when running tests
- **Ruby stdlib not found**: Ensure `core/assets/files/ruby-stdlib.zip` exists
- **JNI crashes**: Check that global references are properly managed in callback contexts
- **Socket errors**: Verify file descriptors are valid and not closed prematurely
