# JVM Build Guide

## ✅ What Was Built

1. **Native C Library**: `libembedded-ruby.so` for Linux ARM64
   - Location: `build/jvm/lib/libembedded-ruby.so`
   - Architecture: ARM aarch64 (your platform)
   - Tested and working: Ruby 3.1.1

2. **Kotlin Multiplatform JVM JAR**: `ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar`
   - Location: `kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar`
   - Size: ~12 KB
   - Contains: Unified Kotlin API that works on any JVM (Java 11+)

## Using the JVM Library

### 1. Add to Your Project

**Gradle (build.gradle.kts):**
```kotlin
dependencies {
    implementation(files("path/to/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"))
}
```

**Maven:**
```xml
<dependency>
    <groupId>com.scorbutics.rubyvm</groupId>
    <artifactId>ruby-vm-kmp-desktop</artifactId>
    <version>1.0.0-SNAPSHOT</version>
    <scope>system</scope>
    <systemPath>${project.basedir}/path/to/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar</systemPath>
</dependency>
```

### 2. Set Native Library Path

The JAR needs to find `libembedded-ruby.so` at runtime. Three options:

**Option A: System property (recommended for testing)**
```bash
java -Djava.library.path=/path/to/build/jvm/lib \
     -cp your-app.jar:ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
     com.yourapp.Main
```

**Option B: Copy to system library path**
```bash
sudo cp build/jvm/lib/libembedded-ruby.so /usr/local/lib/
sudo ldconfig
```

**Option C: Set LD_LIBRARY_PATH**
```bash
export LD_LIBRARY_PATH=/path/to/build/jvm/lib:$LD_LIBRARY_PATH
java -cp your-app.jar:ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar com.yourapp.Main
```

### 3. Use the API

```kotlin
import com.scorbutics.rubyvm.*

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
val interpreter = RubyInterpreter.create(
    appPath = ".",              // Working directory
    rubyBaseDir = "./ruby",     // Where Ruby stdlib will be extracted
    nativeLibsDir = "./lib",    // Native extensions directory
    listener = listener
)

// Create and execute script
val script = RubyScript.fromContent("""
    puts "Hello from Ruby!"
    puts "2 + 2 = #{2 + 2}"
""")

interpreter.enqueue(script) { exitCode ->
    println("Script completed: $exitCode")
}

// Cleanup
script.destroy()
interpreter.destroy()
```

## Full Example

See: `examples/JvmExample.kt`

To run:
```bash
# 1. Compile the example
kotlinc examples/JvmExample.kt -cp kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar

# 2. Run with library path
kotlin -cp .:kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
       -Djava.library.path=build/jvm/lib \
       examples.JvmExampleKt
```

## Rebuilding

**Rebuild native library:**
```bash
cmake --build build/jvm --config Release
```

**Rebuild Kotlin JAR:**
```bash
./gradlew :ruby-vm-kmp:desktopJar
```

**Clean rebuild everything:**
```bash
./gradlew clean
rm -rf build/jvm
cmake -B build/jvm -DCMAKE_BUILD_TYPE=Release -DBUILD_JNI=OFF -DBUILD_TESTS=ON
cmake --build build/jvm
./gradlew :ruby-vm-kmp:desktopJar
```

## Platform Limitations

### ✅ What Works on Your Platform (Linux ARM64):
- ✅ **C Library**: Full support, production-ready
- ✅ **JVM (Desktop) target**: Full support via JNI
- ✅ **Android target**: Full support via JNI (would work with Android NDK)

### ❌ What Doesn't Work:
- ❌ **Kotlin/Native targets** (iOS, macOS, Linux native): Kotlin/Native doesn't support Linux ARM64 host
  - To build these targets, use a supported build platform (macOS, Linux x64, or Windows)
  - The generated code will work on ARM64, but compilation must happen on supported platforms

## Directory Structure

```
embedded-ruby-vm/
├── build/jvm/        # CMake build output
│   ├── lib/
│   │   └── libembedded-ruby.so  # Your native library
│   └── bin/
│       └── test_core            # Tests
├── kmp/
│   ├── build/libs/
│   │   └── ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar  # Your JAR
│   └── src/
│       ├── commonMain/kotlin/   # Platform-agnostic API
│       ├── desktopMain/kotlin/  # JVM implementation
│       └── androidMain/kotlin/  # Android implementation
└── examples/
    └── JvmExample.kt            # Usage example
```

## Troubleshooting

**Error: `UnsatisfiedLinkError: no embedded-ruby in java.library.path`**
- Solution: Set `-Djava.library.path` to directory containing `libembedded-ruby.so`

**Error: `Failed to load native library`**
- Check that `libembedded-ruby.so` exists and is readable
- Verify architecture matches (use `file libembedded-ruby.so`)
- Check dependencies: `ldd build/jvm/lib/libembedded-ruby.so`

**Ruby stdlib not found**
- The first run extracts Ruby stdlib to `./ruby/`
- Ensure write permissions in the current directory
- Check logs for extraction errors

## Next Steps

- Deploy the JAR to Maven Central or your private repository
- Create a Gradle plugin for easier integration
- Package native libraries with the JAR (using `java.library.path` automatic detection)
- Add more Ruby scripting examples
