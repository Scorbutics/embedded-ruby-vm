# Technical Documentation - Embedded Ruby VM

This document provides comprehensive technical details for developers working on the Embedded Ruby VM project.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [API Layers](#api-layers)
3. [Recent Improvements](#recent-improvements)
4. [Development Guidelines](#development-guidelines)
5. [Multi-Platform Considerations](#multi-platform-considerations)
6. [Testing Strategy](#testing-strategy)
7. [Build System](#build-system)

---

## Architecture Overview

### Three-Layer Design

The project uses a **layered architecture** where each layer has a specific purpose:

```
┌─────────────────────────────────────────────────────────────┐
│                     Layer 3: High-Level Kotlin              │
│  Purpose: Ergonomic APIs for common use cases               │
│  Files: RubyInterpreterExtensions.kt, ScriptBatch.kt        │
│  Examples: executeBatch(), batch(), executeSync()           │
└─────────────────────────────────────────────────────────────┘
                            ↓ builds on
┌─────────────────────────────────────────────────────────────┐
│                   Layer 2: Low-Level Kotlin                 │
│  Purpose: Direct wrappers over C API                        │
│  Files: RubyInterpreter.kt, RubyScript.kt                   │
│  Examples: enqueue(), execute(latch)                        │
└─────────────────────────────────────────────────────────────┘
                            ↓ wraps
┌─────────────────────────────────────────────────────────────┐
│                      Layer 1: C Core API                    │
│  Purpose: Foundation - Ruby VM integration                  │
│  Files: ruby-vm.h, ruby-vm.c                                │
│  Examples: ruby_vm_enqueue(), ruby_vm_execute_sync()        │
└─────────────────────────────────────────────────────────────┘
```

### Why This Design?

**Layer 1 (C Core):**
- Provides the foundation
- Platform-agnostic Ruby VM integration
- Thread management, IPC, asset loading
- Direct Ruby C API integration

**Layer 2 (Low-Level Kotlin):**
- Thin wrapper over C API
- Provides building blocks for advanced use cases
- Enables custom synchronization patterns
- Essential for edge cases (e.g., coordinating Ruby scripts with external systems)

**Layer 3 (High-Level Kotlin):**
- Makes common tasks easy (80% use cases)
- Eliminates boilerplate
- Type-safe result handling
- Multi-platform compatible convenience methods

**Key Principle:** "Easy things should be easy, hard things should be possible."

---

## API Layers

### When to Use Each Layer

| Use Case | Recommended Layer | Example |
|----------|------------------|---------|
| Execute multiple scripts | Layer 3 | `executeBatch(scripts)` |
| Named scripts with metrics | Layer 3 | `batch().addScript(...).execute()` |
| Simple blocking execution | Layer 3 | `executeSync(script)` |
| Custom external sync | Layer 2 | `execute(script, latch)` |
| Low-level Ruby VM control | Layer 1 | `ruby_vm_enqueue()` |

### Layer Design Philosophy

**Don't flatten the layers.** Each serves a purpose:

```kotlin
// Layer 3: High-level (recommended for most users)
val results = interpreter.executeBatch(scripts)

// Layer 2: Low-level (for power users)
val latch = CountDownLatch(scripts.size + externalTasks)
scripts.forEach { interpreter.execute(it, latch) { ... } }
externalSystem.work { latch.countDown() }
latch.await()

// Layer 1: C API (internal implementation)
ruby_vm_enqueue(vm, script, callback)
```

## Development Guidelines

> WARNING: Please note that ALL of your commands must be executed INSIDE the docker container!
>
> That means you will have to prefix every exposed command down here with `docker exec <container_name>` (the container name should be `embedded-ruby-vm-dev`).
>
> The docker container stack mounted using docker-compose.yml is using a named volume and not a bind mount. In order to sync the sources, each time you are doing a source code modification, you have to remove the `source-sync-in` (`docker-compose run --rm source-sync-in`) container which will trigger a resync next build.
> Alternatively, you can also use the `docker-dev.sh` script for convenient usage.

### Adding New Features

#### Decision Tree: Where to Add?

```
Does it require new Ruby VM functionality, or can it be beneficial to be included in the C API?
├─ YES → Add to Layer 1 (C Core)
│   ├─ Update ruby-vm.h
│   ├─ Implement in ruby-vm.c
│   ├─ Add JNI bindings (jni/ruby_vm_jni.c)
│   ├─ Add Kotlin wrappers (Layer 2)
│   └─ Optionally add Layer 3 convenience methods
│
└─ NO → Add to Layer 3 (Kotlin conveniences)
    ├─ Define interface in commonMain
    ├─ Implement for JVM in jvmMain
    ├─ Implement for Native in nativeMain
    └─ Add examples and documentation
```

#### Example: Adding a "Warmup" Feature

**If it requires C changes** (e.g., pre-compile scripts):
1. Add `ruby_vm_warmup()` to `core/ruby-vm/ruby-vm.h`
2. Implement in `core/ruby-vm/ruby-vm.c`
3. Add JNI binding: `Java_com_scorbutics_rubyvm_RubyVMNative_warmup()`
4. Add Kotlin wrapper: `fun RubyInterpreter.warmup()`
5. Optionally add convenience: `fun RubyInterpreter.warmupBatch(scripts: List<String>)`

**If it's just a Kotlin pattern** (e.g., run init scripts on startup):
```kotlin
// Just add to Layer 3
fun RubyInterpreter.warmup(initScripts: List<String>) {
    executeBatch(initScripts, timeoutSeconds = 60)
}
```

### Code Style

**Kotlin:**
- Use `actual`/`expect` for multi-platform APIs
- Prefer extension functions over static methods
- Use sealed classes for type-safe error handling
- Document with KDoc

**C:**
- Use consistent naming: `ruby_vm_*`, `ruby_script_*`
- Document with Doxygen-style comments
- Keep platform-specific code isolated
- Error handling via `RubyVMError`

---

### Cross-Platform Time APIs

**JVM:**
```kotlin
val time = System.currentTimeMillis()
```

**Native:**
```kotlin
import platform.posix.*

fun getTimeMillis(): Long {
    val ts = timespec()
    clock_gettime(CLOCK_MONOTONIC, ts.ptr)
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L
}
```

### Cross-Platform File APIs

**JVM:**
```kotlin
import java.io.File
val content = File(path).readText()
```

**Native:**
```kotlin
import kotlin.io.path.*
val content = Path(path).readText()
```

---

## Testing Strategy

### Test Organization

```
tests/
├── native/
│   ├── core/          # Core C library tests
│   ├── jni/           # JNI bridge tests
│   └── jni-android/   # Android-specific tests
└── kmp/
    └── desktopTest/   # Kotlin Multiplatform tests
```

### Testing Layers

**Layer 1 (C Core):**
- Unit tests in `tests/native/core/`
- Use CMocka or similar C testing framework
- Test Ruby VM lifecycle, script execution, error handling

**Layer 2 (Low-Level Kotlin):**
- KMP tests in `kmp/src/desktopTest/`
- Test basic enqueue/execute functionality
- Test resource cleanup

**Layer 3 (High-Level Kotlin):**
- Integration tests in examples
- Test batch execution, timeouts, error handling
- Test metrics calculation

### Running Tests

```bash
# Build and run all tests
./gradlew build

# C tests
cd build
./bin/test_core
./bin/test_jni

# Kotlin tests
./gradlew :ruby-vm-kmp:desktopTest
```

---

## Build System

### CMake Integration

The project uses **CMake for native code** and **Gradle for Kotlin/Java**:

```
Root build.gradle.kts
    ↓ configures
kmp/build.gradle.kts
    ↓ invokes CMake via tasks
CMakeLists.txt
    ↓ builds
Native libraries (.so, .dylib, .a)
```

### CMake Tasks

```bash
# Build native libraries for specific platform
./gradlew buildNativeLibsDesktop
./gradlew buildNativeLibsAndroid
./gradlew buildNativeLibsIOS
./gradlew buildNativeLibsMacOS
./gradlew buildNativeLibsLinux

# Architecture selection
./gradlew build -PtargetArch=x86_64
./gradlew build -PtargetArch=arm64
./gradlew build -PtargetArch=all

# Debug/Release
./gradlew build -PbuildType=Debug
./gradlew build -PbuildType=Release

# With AddressSanitizer
./gradlew build -PenableASAN=true
```

### Gradle Configuration

Key files:
- `build.gradle.kts` - Root configuration
- `kmp/build.gradle.kts` - KMP module with CMake integration
- `settings.gradle.kts` - Project structure

### Automatic Build Flow

```
gradle build
    ↓
Detect platform & arch
    ↓
Run CMake to compile C code
    ↓
Compile Kotlin code
    ↓
Link native libraries (JNI/cinterop)
    ↓
Package JAR/AAR/Framework
```

---

## Key Design Decisions

### 1. Why Keep Low-Level API?

**Decision:** Keep both Layer 2 and Layer 3 APIs

**Reasoning:**
- Layer 3 handles 80% of use cases (batch execution, simple workflows)
- Layer 2 enables 20% edge cases (custom synchronization with external systems)
- Maintenance cost is negligible (15 lines of stable code)
- Removing it would force users into ugly workarounds

**Example Edge Case:**
```kotlin
// User needs to coordinate Ruby scripts with external systems
val latch = CountDownLatch(3)  // 2 Ruby + 1 external

interpreter.execute(script1, latch) { ... }
interpreter.execute(script2, latch) { ... }
externalDatabase.executeAsync { latch.countDown() }

latch.await()  // Wait for ALL operations
```

This cannot be done with Layer 3 APIs alone.

### 2. Why Kotlin Wrappers Instead of C Extensions?

**Decision:** Add convenience features in Kotlin (Layer 3) rather than C (Layer 1)

**Reasoning:**
- C API should stay minimal and focused on Ruby VM integration
- Kotlin is better for high-level patterns (builders, sealed classes, extensions)
- Easier to maintain and test
- No cross-platform C synchronization headaches
- Users can contribute without knowing C

**Result:** Clean separation of concerns.

### 3. Why Multi-Platform Kotlin?

**Decision:** Use Kotlin Multiplatform instead of separate Android/iOS codebases

**Reasoning:**
- Unified API across platforms
- Share business logic
- Single source of truth
- Platform-specific implementations where needed (JNI vs cinterop)

---

## Troubleshooting

### Common Issues

**Native library not found:**
```bash
# Build native libraries first
./gradlew :ruby-vm-kmp:desktopJar -PbuildType=Debug
```

**Memory errors:**
```bash
# Use AddressSanitizer
./gradlew build -PenableASAN=true -PforceRebuild=true
cd examples/kotlin-jvm
../../gradlew runExampleWithASAN
```

**CMake errors:**
```bash
# Clean and rebuild
./gradlew clean
./gradlew build -PforceRebuild=true
```

---

## Future Enhancements

Potential areas for future improvement:

### Coroutine Support (Advanced)

Add Kotlin coroutines support for async/await style:

```kotlin
suspend fun RubyInterpreter.executeAsync(script: String): Int {
    return suspendCancellableCoroutine { continuation ->
        execute(script) { exitCode ->
            continuation.resume(exitCode)
        }
    }
}

// Usage
val results = coroutineScope {
    scripts.map { async { interpreter.executeAsync(it) } }.awaitAll()
}
```

### Script Cancellation

Add ability to cancel running scripts:

```kotlin
val handle = interpreter.executeAsync(longRunningScript)
// Later...
handle.cancel()
```

### Ruby-to-Kotlin Communication

Enable Ruby scripts to call back into Kotlin:

```ruby
# In Ruby
Kotlin.call_method("MyClass", "myMethod", arg1, arg2)
```

---

## Resources

- **Main Documentation:** [README.md](README.md)
- **API Improvements:** [API_IMPROVEMENTS.md](API_IMPROVEMENTS.md)
- **Examples:** [examples/kotlin-jvm/README.md](examples/kotlin-jvm/README.md)
- **Tests:** [tests/README.md](tests/README.md)

---

## Contributing

When contributing, follow these guidelines:

1. **Maintain backward compatibility** - Layer 2 APIs are stable
2. **Add tests** for new functionality
3. **Update documentation** (README.md, API_IMPROVEMENTS.md, this file)
4. **Follow existing patterns** (expect/actual for multi-platform)
5. **Consider all platforms** (JVM, Native) when adding features

For questions or discussions, open an issue on GitHub.

---

**Last Updated:** December 2024
**Project Version:** 1.0.0-SNAPSHOT
