# Tests

This directory contains all tests for the embedded-ruby-vm project, organized by technology.

## Structure

```
tests/
└── native/           # Native C tests (CMake-based)
    ├── core/         # Core Ruby VM tests
    ├── jni/          # JNI layer tests
    └── jni-android/  # Android logging tests
```

## Running Tests

### Native C Tests

The native tests are built automatically with CMake during the build process.

**Build and run:**
```bash
# From project root
./gradlew build

# Run tests
cd build/bin
./test_core           # Core library tests
./test_jni            # JNI layer tests
./test_jni_android    # Android logging tests (if built)
```

**Build configuration:**
- `test_core` - Always built
- `test_jni` - Built when `BUILD_JNI=ON`
- `test_jni_android` - Built when `BUILD_JNI_ANDROID_LOG=ON`

### KMP Tests

Kotlin Multiplatform tests are located in the KMP module:
- **Desktop tests:** `kmp/src/desktopTest/`
- **Common tests:** `kmp/src/commonTest/` (if added)

**Run KMP tests:**
```bash
./gradlew :ruby-vm-kmp:desktopTest
```

## Test Categories

### 1. Core Tests (`native/core/`)
Tests the core Ruby VM C library:
- Ruby interpreter creation and destruction
- Script execution
- Logging callbacks
- Error handling
- Memory management

### 2. JNI Tests (`native/jni/`)
Tests the JNI layer:
- Weak symbol logging mechanism
- Custom logging implementation
- JNI logging functions (`jni_log_write`, `jni_log_printf`)
- No-op default behavior

### 3. Android Tests (`native/jni-android/`)
Tests Android-specific functionality:
- Android logcat integration
- `__android_log_write` binding
- Priority level mapping

### 4. KMP Tests (`kmp/src/desktopTest/`)
Tests Kotlin Multiplatform integration:
- Native library loading
- JNI bindings
- Platform-specific implementations

## Adding New Tests

### Adding a Native C Test

1. Create test file in appropriate directory:
   ```bash
   touch tests/native/core/test_new_feature.c
   ```

2. Update `tests/native/core/CMakeLists.txt` to include the new test

3. Build and run:
   ```bash
   ./gradlew build
   cd build/bin
   ./test_new_feature
   ```

### Adding a KMP Test

1. Create test file in `kmp/src/desktopTest/kotlin/`:
   ```kotlin
   // kmp/src/desktopTest/kotlin/com/scorbutics/rubyvm/NewTest.kt
   package com.scorbutics.rubyvm

   import kotlin.test.Test
   import kotlin.test.assertTrue

   class NewTest {
       @Test
       fun testNewFeature() {
           // Your test code
           assertTrue(true)
       }
   }
   ```

2. Run tests:
   ```bash
   ./gradlew :ruby-vm-kmp:desktopTest
   ```

## Test Output

### Native Tests
- **Exit code:** 0 for success, non-zero for failure
- **Logs:** Console output + `ruby_vm_test.log` (for core tests)
- **Location:** Built in `build/bin/`

### KMP Tests
- **Reports:** `kmp/build/test-results/`
- **HTML Report:** `kmp/build/reports/tests/desktopTest/index.html`

## Continuous Integration

Tests are automatically run during CI builds:
```bash
./gradlew build test
```

This runs:
- All native C tests
- All KMP tests
- Platform-specific tests (based on build configuration)

## Troubleshooting

### Native Tests Fail to Build

**Problem:** CMake can't find test directories

**Solution:**
```bash
# Clean build
rm -rf build
./gradlew clean build
```

### Tests Can't Find Ruby Stdlib

**Problem:** Tests fail with "Ruby stdlib not found"

**Solution:**
```bash
# Extract Ruby stdlib
unzip core/assets/files/ruby-stdlib.zip -d ruby
```

### JNI Tests Fail

**Problem:** Native library not found

**Solution:**
```bash
# Rebuild native libraries
./gradlew :ruby-vm-kmp:buildNativeLibsDesktop
```

## See Also

- **Examples:** See `examples/README.md` for user-facing examples
- **Build System:** See root `README.md` for build configuration
- **API Documentation:** See `CLAUDE.md` for detailed API reference
