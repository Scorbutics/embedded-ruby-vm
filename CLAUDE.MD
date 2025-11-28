# Embedded Ruby VM - Technical Documentation

> **Note**: For a quick overview and getting started guide, see [README.md](README.md). This document provides comprehensive technical details for developers working on the project.

## Table of Contents

- [Project Overview](#project-overview)
- [Architecture](#architecture)
  - [Multi-Layer Architecture](#multi-layer-architecture)
  - [Core Components](#core-components)
  - [Threading Model](#threading-model)
  - [Build System](#build-system)
- [Key APIs](#key-apis)
  - [C API](#c-api)
  - [Kotlin Multiplatform API](#kotlin-multiplatform-api-unified-api-for-all-platforms)
  - [JNI API](#jni-api-low-level-used-internally-by-kmp)
  - [JNI Logging Architecture](#jni-logging-architecture)
- [Development Guidelines](#development-guidelines)
  - [Working with the Codebase](#working-with-the-codebase)
  - [Common Tasks](#common-tasks)
  - [Architecture Decisions](#architecture-decisions)
- [Directory Structure](#directory-structure)
- [Files of Interest](#files-of-interest)
- [Platform Support](#platform-support)
- [External Dependencies](#external-dependencies)
- [Troubleshooting](#troubleshooting)

## Project Overview

This is a C library that embeds a full Ruby 3.1.0 interpreter for use in native applications, Android, iOS, and JVM applications. It provides a complete Ruby runtime environment with the standard library embedded directly into the binary, allowing Ruby script execution without requiring an external Ruby installation.

The project is built using Kotlin Multiplatform (KMP) to provide a unified API across all platforms, with native bridges using JNI (for Android/JVM) and cinterop (for iOS/macOS/Linux native targets).

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
- **Android** (via JNI) - ✅ Fully Supported
  - ABIs: arm64-v8a, armeabi-v7a, x86, x86_64
  - API Level: 24+
- **JVM Desktop** (via JNI) - ✅ Fully Supported
  - Windows, macOS, Linux
  - Architectures: x86_64, arm64
  - Java 11+
- **iOS** (via cinterop) - ⚠️ Currently Disabled
  - arm64 (device), x64/arm64 (simulator)
  - Can be re-enabled on supported build hosts
- **macOS** (via cinterop) - ⚠️ Currently Disabled
  - arm64 (Apple Silicon), x64 (Intel)
  - Can be re-enabled on supported build hosts
- **Linux Native** (via cinterop) - ⚠️ Currently Disabled
  - x64
  - Can be re-enabled on supported build hosts

> **Note**: Native targets (iOS, macOS, Linux) are currently disabled in `kmp/build.gradle.kts` because the host platform (Linux ARM64) is not supported by Kotlin/Native. To re-enable these targets, build on a supported platform (macOS, Linux x64, or Windows) and uncomment the relevant sections in the build file.

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

**Fully Integrated Gradle + CMake Build System:**

The project uses an automated two-tier build system where **Gradle automatically invokes CMake** - you never need to run CMake manually!

1. **Gradle** (top-level) - Kotlin Multiplatform orchestration
   - Automatically invokes CMake before compilation
   - Supports architecture selection via `-PtargetArch`
   - Supports build type selection via `-PbuildType`
   - Manages Kotlin compilation and packaging
   - Generates platform-specific artifacts (JAR, AAR, XCFramework)

2. **CMake** (native layer) - C library compilation
   - Minimum version: 3.19.2
   - Automatically invoked by Gradle tasks
   - Build Options:
     - `BUILD_JNI`: Enable JNI bindings (auto-set by Gradle)
     - `BUILD_JNI_ANDROID_LOG`: Enable Android logging (auto-set for Android)
     - `BUILD_TESTS`: Build test suite (default: ON)
     - `CMAKE_BUILD_TYPE`: Debug or Release (controlled by `-PbuildType`)
   - External Dependencies:
     - Ruby shared library in `core/external/lib/{host}/`
     - minizip-ng (fetched automatically)

**Quick Start - Build Everything with Gradle:**
```bash
# Build for current architecture (auto-detected)
./gradlew build

# Build for specific architecture
./gradlew build -PtargetArch=x86_64
./gradlew build -PtargetArch=arm64
./gradlew build -PtargetArch=all

# Debug build
./gradlew build -PbuildType=Debug
```

**Platform-Specific Builds:**
```bash
# Android (with architecture selection)
./gradlew :ruby-vm-kmp:assembleRelease -PtargetArch=arm64

# JVM Desktop (auto-builds native libraries)
./gradlew :ruby-vm-kmp:desktopJar -PtargetArch=x86_64

# iOS
./gradlew :ruby-vm-kmp:linkDebugFrameworkIosArm64

# Build native libraries only (all platforms)
./gradlew buildAllNativeLibs -PtargetArch=all
```

**Architecture Selection:**
- `x86_64` - 64-bit Intel/AMD (default on x64 systems)
- `arm64` - 64-bit ARM (default on ARM systems)
- `all` - Build for all supported architectures

**Advanced CMake-only Build (for C library development):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

See [BUILD_GUIDE.md](BUILD_GUIDE.md) for comprehensive build documentation.

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

## Directory Structure

```
embedded-ruby-vm/
├── .claude/                   # Claude AI configuration
├── .git/                      # Git repository data
├── .gitignore                 # Git ignore rules
├── .gradle/                   # Gradle build cache
├── _deps/                     # CMake fetched dependencies (minizip-ng)
├── build-test/                # Test build artifacts
├── core/                      # C library core implementation
│   ├── assets/               # Embedded Ruby stdlib and scripts
│   │   ├── files/            # Asset files (ruby-stdlib.zip, fifo_interpreter.rb)
│   │   ├── embed_binary.cmake # CMake script for embedding binaries
│   │   ├── install.c/h       # Asset installation logic
│   │   └── use_direct_memory.c/h # Direct memory access for embedded assets
│   ├── external/             # External dependencies
│   │   └── lib/              # Ruby shared libraries by platform
│   │       ├── aarch64-linux-android/
│   │       └── x86_64-linux-gnu/
│   ├── logging/              # Logging system
│   │   └── logging.c/h
│   ├── ruby-vm/              # Ruby VM wrapper and communication
│   │   ├── exec-main-vm.c    # VM initialization and execution
│   │   ├── ruby-interpreter.c/h # High-level interpreter API
│   │   ├── ruby-vm.c/h       # VM lifecycle management
│   │   ├── ruby-script.c/h   # Script representation
│   │   └── ruby-comm-channel.c/h # Socket-based communication
│   └── CMakeLists.txt        # Core library CMake configuration
├── examples/                  # Usage examples
│   ├── JvmExample.kt         # Kotlin/JVM example
│   ├── SimpleJavaExample.java # Java example
│   └── JvmExample.jar        # Compiled example JAR
├── gradle/                    # Gradle wrapper files
│   ├── libs.versions.toml    # Dependency version catalog
│   └── wrapper/              # Gradle wrapper binaries
├── jni/                       # JNI bindings for Android/JVM
│   ├── android/              # Android-specific implementations
│   │   └── android_logging.c # Android logcat integration
│   ├── ruby_vm_jni.c/h       # Main JNI bridge
│   ├── jni_logging.c/h       # Platform-agnostic logging (weak symbols)
│   ├── jni_ruby_info.c/h     # Ruby version info
│   ├── get_activity_parameters.c/h # Android activity integration
│   └── CMakeLists.txt        # JNI layer CMake configuration
├── kmp/                       # Kotlin Multiplatform module
│   ├── build/                # Build artifacts
│   │   ├── cmake/            # CMake build directories per platform
│   │   ├── generated/        # Generated sources and resources
│   │   └── libs/             # Compiled native libraries
│   ├── libs/                 # Native library output directories
│   │   ├── android/          # Android ABIs (arm64-v8a, etc.)
│   │   ├── linux_x64/        # Linux x86_64
│   │   ├── linux_arm64/      # Linux ARM64
│   │   ├── macos_x64/        # macOS Intel
│   │   ├── macos_arm64/      # macOS Apple Silicon
│   │   └── windows_x64/      # Windows x64
│   ├── src/                  # Kotlin source code
│   │   ├── commonMain/       # Platform-agnostic API (expect declarations)
│   │   │   └── kotlin/com/scorbutics/rubyvm/
│   │   │       ├── RubyInterpreter.kt
│   │   │       ├── RubyScript.kt
│   │   │       └── LogListener.kt
│   │   ├── androidMain/      # Android implementation (JNI)
│   │   │   └── kotlin/com/scorbutics/rubyvm/
│   │   ├── desktopMain/      # JVM Desktop implementation (JNI)
│   │   │   └── kotlin/com/scorbutics/rubyvm/
│   │   ├── nativeMain/       # Native implementation (cinterop, disabled)
│   │   │   └── kotlin/com/scorbutics/rubyvm/
│   │   └── nativeInterop/    # cinterop definitions
│   │       └── cinterop/ruby_vm.def
│   └── build.gradle.kts      # KMP build configuration with CMake integration
├── META-INF/                  # Metadata files
├── ruby/                      # Ruby runtime directory (extracted at runtime)
│   └── ruby/                 # Ruby stdlib files
│       ├── 3.1.0/            # Ruby version-specific files
│       └── gems/             # Ruby gems
├── tests/                     # Test suites
│   ├── core/                 # Core library tests
│   │   └── test_core.c
│   ├── jni/                  # JNI layer tests
│   │   └── test_jni.c
│   ├── jni-android/          # Android logging tests
│   │   └── test_jni_android_log.c
│   └── CMakeLists.txt        # Tests CMake configuration
├── CMakeLists.txt             # Root CMake configuration
├── build.gradle.kts           # Root Gradle build file
├── settings.gradle.kts        # Gradle project settings
├── gradle.properties          # Gradle configuration
├── gradlew                    # Gradle wrapper script (Unix)
├── gradlew.bat                # Gradle wrapper script (Windows)
├── run-jvm-example.sh         # Script to run JVM example
├── dummy.c                    # Dummy file for CMake
├── ruby_vm_test.log           # Test output log
├── CLAUDE.md                  # This file - technical documentation
├── QUICK_START.md             # Quick start guide
├── README.md                  # Project overview and getting started
└── LICENSE                    # MIT License
```

### Key Directories Explained

- **`core/`**: The heart of the project - pure C implementation of the Ruby VM wrapper
- **`jni/`**: JNI bridge layer for Android and JVM Desktop platforms
- **`kmp/`**: Kotlin Multiplatform module providing unified API across platforms
- **`kmp/libs/`**: Compiled native libraries for each platform/architecture
- **`kmp/build/cmake/`**: CMake build directories (auto-generated by Gradle)
- **`examples/`**: Working examples demonstrating library usage
- **`tests/`**: Comprehensive test suites for all layers
- **`ruby/`**: Runtime directory where Ruby stdlib is extracted (created at runtime)

## Files of Interest

### Documentation
- `README.md`: Project overview, quick start, and usage examples
- `CLAUDE.md`: This file - comprehensive technical documentation
- `QUICK_START.md`: Quick start guide with build examples
- `LICENSE`: MIT License

### Kotlin Multiplatform Layer
- `kmp/build.gradle.kts`: KMP module configuration with platform targets and CMake integration
- `kmp/src/commonMain/kotlin/`: Shared Kotlin API (expect declarations)
  - `RubyInterpreter.kt`: Main interpreter interface
  - `RubyScript.kt`: Script representation
  - `LogListener.kt`: Logging callback interface
- `kmp/src/androidMain/kotlin/`: Android implementations (JNI-based)
  - `RubyInterpreter.android.kt`
  - `RubyScript.android.kt`
- `kmp/src/desktopMain/kotlin/`: JVM Desktop implementations (JNI-based)
  - `RubyInterpreter.jvm.kt`
  - `RubyScript.jvm.kt`
- `kmp/src/nativeMain/kotlin/`: Native implementations (cinterop-based, currently disabled)
  - `RubyInterpreter.native.kt`: iOS/macOS/Linux implementation
  - `RubyScript.native.kt`
- `kmp/src/nativeInterop/cinterop/ruby_vm.def`: cinterop configuration

### Build System
- `settings.gradle.kts`: Gradle project configuration
- `build.gradle.kts`: Root Gradle build file
- `gradle.properties`: Gradle settings (parallel builds, caching, etc.)
- `gradle/libs.versions.toml`: Version catalog for dependencies
- `CMakeLists.txt`: Root CMake configuration
- `core/CMakeLists.txt`: Core library CMake configuration
- `core/assets/embed_binary.cmake`: Asset embedding logic

### Core C Library
- `core/ruby-vm/exec-main-vm.c`: Ruby VM initialization and main execution loop
- `core/ruby-vm/ruby-interpreter.c/h`: High-level interpreter API
- `core/ruby-vm/ruby-vm.c/h`: VM lifecycle management
- `core/ruby-vm/ruby-script.c/h`: Script representation and management
- `core/ruby-vm/ruby-comm-channel.c/h`: Communication channel implementation
- `core/assets/files/fifo_interpreter.rb`: Ruby-side script executor
- `core/assets/install.c/h`: Asset installation and extraction
- `core/logging/logging.c/h`: Logging system implementation

### JNI Layer
- `jni/ruby_vm_jni.c/h`: JNI bridge implementation
- `jni/jni_logging.c/h`: Weak symbol logging interface
- `jni/android/android_logging.c`: Android logging implementation (strong symbol)
- `jni/jni_ruby_info.c/h`: Ruby version info
- `jni/get_activity_parameters.c/h`: Android activity integration

### Tests
- `tests/core/test_core.c`: Core library tests
- `tests/jni/test_jni.c`: JNI layer tests (weak symbol verification)
- `tests/jni-android/test_jni_android_log.c`: Android logging tests

### Examples
- `examples/JvmExample.kt`: Kotlin/JVM usage example
- `examples/SimpleJavaExample.java`: Java usage example
- `run-jvm-example.sh`: Script to run the JVM example

## Platform Support

### Kotlin Multiplatform Targets

**Currently Active:**
- **Android**: API 24+ (arm64-v8a, armeabi-v7a, x86, x86_64)
- **JVM Desktop**: Java 11+ (Windows, macOS, Linux - x86_64, arm64)

**Currently Disabled** (can be re-enabled on supported build hosts):
- **iOS**: 13.0+ (arm64 device, x64/arm64 simulator) - XCFramework
- **macOS**: 10.13+ (arm64, x64)
- **Linux Native**: x64

### Native C Library Support
- **Ruby Version**: 3.1.0
- **Platforms**: Linux (Termux/Android), Android (via JNI), iOS, macOS, Windows
- **Architectures**: aarch64 (arm64), x86_64
- **Host format**: `{arch}-{platform}-{os}` (e.g., `aarch64-linux-android`)

### Build Host Requirements
- **For Android/JVM**: Any platform with JDK 11+ and Android NDK (for Android)
- **For iOS/macOS**: macOS with Xcode
- **For Linux Native**: Linux x64 or macOS
- **For Windows**: Windows with Visual Studio or MinGW

## External Dependencies

- **Ruby**: Shared library required in `core/external/lib/{host}/libruby.so`
- **minizip-ng**: Fetched automatically by CMake for zip handling
- **pthreads**: For threading support
- **zlib**: Required for JNI builds (zip handling)

**Platform-Specific:**
- **Android**: `liblog` (automatically linked by `jni/android/` module when `BUILD_JNI_ANDROID_LOG=ON`)
- **Other platforms**: No platform-specific dependencies (uses no-op logging by default)

## Troubleshooting

### Build Issues

**"JAVA_HOME not set"**
```bash
# Linux/macOS
export JAVA_HOME=/path/to/jdk

# Windows
set JAVA_HOME=C:\Path\To\JDK
```

**"ANDROID_NDK_HOME not set" (Android builds only)**
```bash
# Linux/macOS
export ANDROID_NDK_HOME=/path/to/android-ndk

# Windows
set ANDROID_NDK_HOME=C:\Path\To\Android\NDK
```

**"CMake not found"**
- Install CMake 3.19.2 or higher
- Ensure CMake is in your system PATH

**Force Rebuild**
```bash
./gradlew clean
rm -rf kmp/build/cmake/
./gradlew build
```

**Gradle daemon issues**
```bash
./gradlew --stop
./gradlew build
```

### Runtime Issues

**"Ruby stdlib not found"**
- Ensure `core/assets/files/ruby-stdlib.zip` exists
- Check that the asset extraction is working correctly
- Verify the `rubyBaseDir` path in your code

**"Native library not found" (JVM Desktop)**
- The library should be automatically extracted from the JAR
- Check that `libembedded-ruby.so/.dylib/.dll` is in the JAR resources
- Verify the native library was built for your platform/architecture

**Logging Issues**
- Check `ruby_vm_test.log` for Ruby output when running tests
- For Android: Use `adb logcat` to see native logs
- Ensure LogListener callbacks are properly implemented

**JNI crashes**
- Check that global references are properly managed in callback contexts
- Verify JNI method signatures match between Java and C
- Enable JNI checks: `-Xcheck:jni`

**Socket/Communication errors**
- Verify file descriptors are valid and not closed prematurely
- Check for thread synchronization issues
- Ensure the Ruby VM thread is running

**Script execution hangs**
- Check for infinite loops in Ruby scripts
- Verify the communication channel is not blocked
- Increase timeout values if needed

### Platform-Specific Issues

**Android**
- Minimum API level is 24
- Ensure correct ABI is built for your device
- Check NDK version compatibility

**iOS/macOS (when enabled)**
- Requires macOS build host
- Xcode command line tools must be installed
- Check deployment target compatibility

**Linux**
- Ensure pthread library is available
- Check for missing system dependencies

**Windows**
- Visual Studio or MinGW required for building
- Ensure correct architecture (x64) is targeted

### Getting Help

1. Check the logs: `ruby_vm_test.log`, logcat (Android), or console output
2. Enable verbose logging in your LogListener implementation
3. Review the examples in `examples/` directory
4. Consult [CLAUDE.md](CLAUDE.md) for detailed technical information
5. Open an issue on GitHub with:
   - Your platform and architecture
   - Build command used
   - Full error message and stack trace
   - Relevant log files
