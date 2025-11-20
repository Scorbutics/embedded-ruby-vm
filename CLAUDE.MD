# Embedded Ruby VM

## Project Overview

This is a C library that embeds a full Ruby interpreter for use in native applications and Android apps. It provides a complete Ruby runtime environment with the standard library embedded directly into the binary, allowing Ruby script execution without requiring an external Ruby installation.

## Architecture

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

- **CMake** (minimum version 3.19.2)
- **Build Options**:
  - `BUILD_ANDROID_JNI`: Enable Android JNI bindings (default: OFF)
  - `BUILD_TESTS`: Build test executable (default: ON)
- **Host-specific**: Builds are configured for specific platform triplets (e.g., `aarch64-linux-android`)
- **External Dependencies**:
  - Ruby shared library (`libruby.so`) expected in `core/external/lib/{host}/`
  - minizip-ng fetched as dependency

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

### JNI API (Kotlin/Java)

```java
// Package: com.scorbutics.rubyvm
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
- **Automatic**: Android logging is automatically enabled when building with `BUILD_ANDROID_JNI=ON`
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

**Building the Project**:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Building with Android JNI**:
```bash
cmake -B build -DBUILD_ANDROID_JNI=ON
cmake --build build
```

**Running Tests**:
```bash
cd build
ctest
# or run directly:
./bin/test
```

### Architecture Decisions

1. **Why Socket Communication?**
   - Isolates Ruby VM in separate process/thread
   - Prevents crashes in Ruby from affecting main application
   - Clean separation of concerns

2. **Why Embed Assets?**
   - Single-binary deployment
   - No external Ruby installation required
   - Consistent stdlib version across platforms

3. **Threading vs Process**
   - Currently uses threads with socket pairs
   - Could be adapted to use separate processes for better isolation

## Files of Interest

- `core/ruby-vm/exec-main-vm.c`: Ruby VM initialization and main execution loop
- `core/assets/files/fifo_interpreter.rb`: Ruby-side script executor
- `tests/test.c`: Example usage and integration test
- `jni/ruby_vm_jni.c`: Android JNI bridge implementation
- `jni/jni_logging.c/h`: Weak symbol logging interface
- `jni/android/android_logging.c`: Android logging implementation (strong symbol)
- `core/assets/embed_binary.cmake`: Asset embedding logic

## Platform Support

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
- **Android**: `liblog` (automatically linked by `jni/android/` module when `BUILD_ANDROID_JNI=ON`)
- **Other platforms**: No platform-specific dependencies (uses no-op logging by default)

## Troubleshooting

- **Logging**: Check `ruby_vm_test.log` for Ruby output when running tests
- **Ruby stdlib not found**: Ensure `core/assets/files/ruby-stdlib.zip` exists
- **JNI crashes**: Check that global references are properly managed in callback contexts
- **Socket errors**: Verify file descriptors are valid and not closed prematurely
