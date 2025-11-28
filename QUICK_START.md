# Quick Start Guide - Automated Gradle Build System

## One-Command Build

```bash
# Build everything for your current architecture
./gradlew build
```

That's it! Gradle will automatically:
1. Detect your OS and architecture
2. Run CMake to compile native libraries
3. Compile Kotlin code
4. Package everything into distributable artifacts

## Choose Your Architecture

### Build for x86_64 (Intel/AMD)
```bash
./gradlew build -PtargetArch=x86_64
```

### Build for arm64 (ARM/Apple Silicon)
```bash
./gradlew build -PtargetArch=arm64
```

### Build for All Architectures
```bash
./gradlew build -PtargetArch=all
```

## Platform-Specific Examples

### Android Development
```bash
# Quick iteration (arm64 only)
./gradlew :ruby-vm-kmp:assembleDebug -PtargetArch=arm64

# Production build (all ABIs)
./gradlew :ruby-vm-kmp:assembleRelease -PtargetArch=all
```

### Desktop JVM (Linux/Mac/Windows)
```bash
# Build JAR with embedded native library
./gradlew :ruby-vm-kmp:desktopJar

# For specific architecture
./gradlew :ruby-vm-kmp:desktopJar -PtargetArch=x86_64
```

### iOS Development
```bash
# Build for device
./gradlew buildNativeLibsIOS -PtargetArch=arm64

# Build for simulator
./gradlew buildNativeLibsIOS -PtargetArch=x86_64
```

## Debug vs Release

```bash
# Debug build (includes debug symbols, no optimization)
./gradlew build -PbuildType=Debug

# Release build (optimized, default)
./gradlew build -PbuildType=Release
```

## Common Tasks

### Clean Build
```bash
./gradlew clean build
```

### Build Just Native Libraries
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

### List Available Tasks
```bash
./gradlew tasks --group=build
```

## What's Different?

**Before:**
```bash
# Manual CMake build required
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Then Gradle
./gradlew build
```

**Now:**
```bash
# Just Gradle - CMake runs automatically!
./gradlew build -PtargetArch=x86_64
```

## Troubleshooting

### "JAVA_HOME not set"
```bash
export JAVA_HOME=/path/to/jdk
```

### "ANDROID_NDK_HOME not set" (Android builds only)
```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
```

### Force Rebuild
```bash
./gradlew clean
rm -rf kmp/build/cmake/
./gradlew build
```

## Pro Tips

1. **Use architecture selection for faster iteration:**
   ```bash
   # During development, build only what you need
   ./gradlew build -PtargetArch=x86_64
   ```

2. **Set defaults in gradle.properties:**
   ```properties
   targetArch=x86_64
   buildType=Debug
   ```

3. **Parallel builds:**
   ```bash
   # Build multiple architectures in parallel
   ./gradlew build -PtargetArch=all --parallel
   ```

4. **Check what will be built:**
   ```bash
   ./gradlew build --dry-run
   ```

## Learn More

- [BUILD_GUIDE.md](BUILD_GUIDE.md) - Comprehensive build documentation
- [CLAUDE.md](CLAUDE.md) - Full project documentation
- [ERROR_HANDLING.md](ERROR_HANDLING.md) - Debugging and error handling
