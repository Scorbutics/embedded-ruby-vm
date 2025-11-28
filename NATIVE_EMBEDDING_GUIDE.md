# Native Library Embedding Guide

## ✅ What Was Implemented

The Ruby VM JAR now **embeds native libraries** directly, making it self-contained!

### Before (Manual Setup Required)
```bash
# Users had to set java.library.path
java -Djava.library.path=/path/to/libs \
     -cp ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
     MyApp
```

### After (Just Works™)
```bash
# Native library automatically extracted and loaded
java -cp ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar MyApp
```

## How It Works

### 1. Automatic Native Library Loading

**NativeLibraryLoader.kt**:
- Detects platform (OS + architecture)
- Extracts embedded `.so`/`.dll`/`.dylib` from JAR resources
- Loads the native library automatically
- Falls back to `java.library.path` if extraction fails

**Flow:**
```
JAR resource: /natives/linux-arm64/libembedded-ruby.so
       ↓
Extract to temp: /tmp/ruby-vm-native-xyz/libembedded-ruby.so
       ↓
System.load(tempFile.absolutePath)
       ↓
Native library ready!
```

### 2. Build-Time Packaging

**Gradle task: `packageNativeLibraries`**
- Runs before `desktopJar` compilation
- Copies native libraries to `src/desktopMain/resources/natives/{platform}/`
- Libraries are included in JAR during resource processing

**Supported platforms** (add more by building for each):
- ✅ `linux-arm64`: Linux ARM64 (aarch64) - **Currently included**
- 🔲 `linux-x64`: Linux x86_64
- 🔲 `macos-arm64`: macOS Apple Silicon
- 🔲 `macos-x64`: macOS Intel
- 🔲 `windows-x64`: Windows 64-bit

## JAR Contents

```
ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar
├── com/scorbutics/rubyvm/          # Kotlin classes
│   ├── RubyInterpreter.class
│   ├── RubyScript.class
│   ├── LogListener.class
│   ├── NativeLibraryLoader.class   # 👈 NEW: Auto-loader
│   └── RubyVMNative.class
└── natives/                         # 👈 NEW: Embedded libraries
    └── linux-arm64/
        └── libembedded-ruby.so     # 15 MB native library
```

**JAR Size**: ~15 MB (includes full Ruby stdlib + VM)

## Usage

### No Setup Required!

```kotlin
import com.scorbutics.rubyvm.*

// Just use it - native library loads automatically
val interpreter = RubyInterpreter.create(
    appPath = ".",
    rubyBaseDir = "./ruby",
    nativeLibsDir = "./lib",
    listener = myListener
)
```

### What Happens Under the Hood

1. **First time `RubyVMNative` is accessed**:
   ```kotlin
   init {
       NativeLibraryLoader.loadLibrary()  // Automatic!
   }
   ```

2. **Platform detection**:
   ```
   OS: Linux
   Arch: aarch64
   → Platform: linux-arm64
   ```

3. **Resource lookup**:
   ```
   Check: /natives/linux-arm64/libembedded-ruby.so
   Found: ✓
   ```

4. **Extraction and loading**:
   ```
   Extract to: /tmp/ruby-vm-native-12345/libembedded-ruby.so
   Load: System.load("/tmp/ruby-vm-native-12345/libembedded-ruby.so")
   Success: ✓
   ```

## Adding More Platforms

To support multiple platforms in one JAR:

### 1. Build Native Libraries for Each Platform

```bash
# Linux x64
cmake -B build/linux-x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_JNI=ON
cmake --build build/linux-x64

# macOS ARM64
cmake -B build/macos-arm64 -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_JNI=ON
cmake --build build/macos-arm64

# Windows x64 (cross-compile or build on Windows)
cmake -B build/windows-x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_JNI=ON
cmake --build build/windows-x64
```

### 2. Update build.gradle.kts

The `packageNativeLibraries` task already maps these:

```kotlin
val nativeLibsSource = mapOf(
    "linux-x64" to "../build/linux-x64/lib/libembedded-ruby.so",
    "linux-arm64" to "../build/jvm/lib/libembedded-ruby.so",
    "macos-x64" to "../build/macos-x64/lib/libembedded-ruby.dylib",
    "macos-arm64" to "../build/macos-arm64/lib/libembedded-ruby.dylib",
    "windows-x64" to "../build/windows-x64/lib/embedded-ruby.dll"
)
```

### 3. Rebuild JAR

```bash
./gradlew :ruby-vm-kmp:desktopJar
```

The JAR will automatically include all available platform libraries!

## Benefits

### ✅ For Users
- **Zero configuration**: No `java.library.path` setup
- **Cross-platform**: Single JAR works on all supported platforms
- **Portable**: Just drop the JAR in classpath

### ✅ For Developers
- **Easy distribution**: Ship one JAR file
- **No installation**: No manual library installation
- **Maven/Gradle friendly**: Works seamlessly with dependency managers

### ✅ Technical
- **Clean temp files**: Libraries in temp dir, deleted on JVM exit
- **Fallback support**: Still works with `java.library.path` if needed
- **Platform detection**: Automatic OS and architecture detection

## Example: Maven Distribution

```xml
<!-- Users just add this dependency - native libs included! -->
<dependency>
    <groupId>com.scorbutics.rubyvm</groupId>
    <artifactId>ruby-vm-kmp-desktop</artifactId>
    <version>1.0.0</version>
</dependency>
```

No additional setup required!

## Troubleshooting

### JAR size is too large
**Solution**: Create platform-specific JARs

```bash
# Build only for Linux ARM64
./gradlew :ruby-vm-kmp:desktopJar  # Current: 15 MB

# Or separate JARs per platform:
ruby-vm-kmp-desktop-linux-arm64-1.0.0.jar   # 15 MB
ruby-vm-kmp-desktop-linux-x64-1.0.0.jar     # 15 MB
ruby-vm-kmp-desktop-macos-1.0.0.jar         # 15 MB
ruby-vm-kmp-desktop-windows-1.0.0.jar       # 15 MB
```

### Library not found for my platform
**Check embedded platforms**:
```bash
jar -tf ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar | grep natives
```

**Fallback to java.library.path**:
```bash
java -Djava.library.path=/custom/path -cp app.jar MyApp
```

### Wrong platform detected
The loader automatically detects:
- OS: `System.getProperty("os.name")`
- Architecture: `System.getProperty("os.arch")`

If detection fails, you'll see:
```
UnsupportedOperationException: Unsupported OS: xyz
```

## Architecture

```
Application Code
    ↓
RubyInterpreter API (Kotlin)
    ↓
RubyVMNative.init
    ↓
NativeLibraryLoader.loadLibrary()
    ↓ (platform detection)
Extract JAR resource → /tmp/ruby-vm-native-xyz/
    ↓
System.load(tempFile)
    ↓
✓ JNI bindings ready
    ↓
libembedded-ruby.so (C library)
    ↓
Ruby 3.1.1 VM
```

## Comparison with Other Approaches

| Approach | Setup | Portability | JAR Size |
|----------|-------|-------------|----------|
| **Embedded** (this) | ✅ None | ✅ Excellent | ⚠️ Large (15 MB) |
| java.library.path | ⚠️ Manual | ⚠️ Platform-specific | ✅ Small (12 KB) |
| System install | ❌ Complex | ❌ Poor | ✅ Small (12 KB) |

## Files Changed

1. **NativeLibraryLoader.kt** - Auto-extraction and loading logic
2. **RubyVMNative.kt** - Uses NativeLibraryLoader instead of System.loadLibrary
3. **build.gradle.kts** - Added packageNativeLibraries task
4. **JAR resources** - Now contains /natives/{platform}/ directory

## Next Steps

- ✅ **Current**: Linux ARM64 support
- 🔲 **TODO**: Build for Linux x64, macOS, Windows
- 🔲 **TODO**: Publish to Maven Central
- 🔲 **TODO**: Create platform-specific classifier JARs (optional)
