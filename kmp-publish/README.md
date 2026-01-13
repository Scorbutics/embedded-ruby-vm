# KMP Publishing Module

This is a simplified Gradle project for publishing the embedded Ruby VM as a Kotlin Multiplatform Android library.

## Overview

This project packages a pre-built fat library (`librgss_runtime.a`) into an Android AAR and publishes it to Maven Local for development use.

## Prerequisites

**Note:** Publishing requires the Android SDK and should be run **on your host machine** (outside Docker), as the Docker container only has the Android NDK for cross-compilation.

Requirements:
- Android SDK installed (via Android Studio or sdkmanager)
- `ANDROID_HOME` or `ANDROID_SDK_ROOT` environment variable set
- Java 17 or higher

## Workflow

### Step 1: Build the Fat Library (in Docker)

The fat library must be built first using the main project build system:

```bash
# Configure for target architecture
./configure --toolchain-params=toolchain-params/x86_64-android-toolchain.params

# Build the fat library
make

# The fat library will be at:
# build/target/x86_64-android/usr/local/lib/librgss_runtime.a
```

### Step 2: Publish to Maven Local (on Host)

After building the fat library in Docker, publish the KMP module **on your host machine**:

```bash
cd external/embedded-ruby-vm/kmp-publish

# Publish for the target ABI
./gradlew publishToMavenLocal \
  -PtargetArch=x86_64 \
  -PnativeLibraryName=rgss_runtime
```

The module will be published to:
- **Maven coordinates:** `com.scorbutics.rubyvm:kmp-android:1.0.0-SNAPSHOT`
- **Local path:** `~/.m2/repository/com/scorbutics/rubyvm/kmp-android/1.0.0-SNAPSHOT/`

### Step 3: Use in Android Projects

Add the dependency to your `build.gradle.kts`:

```kotlin
repositories {
    mavenLocal()
}

dependencies {
    implementation("com.scorbutics.rubyvm:kmp-android:1.0.0-SNAPSHOT")
}
```

## How It Works

1. **`copyFatLibrary` task**: Copies the pre-built fat library from `../../../build/target/{targetArch}-android/usr/local/lib/librgss_runtime.a` to `src/main/jniLibs/{targetArch}/`

2. **Source reuse**: The Kotlin source files are referenced from the main KMP module at `../kmp/src/` (no duplication)

3. **Android AAR packaging**: The Android Gradle Plugin packages the Kotlin classes and native library into an AAR

4. **Maven publishing**: The AAR is published to Maven Local for local development

## Architecture Support

Build and publish for each target ABI separately:

```bash
# For x86_64 (emulator)
./gradlew publishToMavenLocal -PtargetArch=x86_64 -PnativeLibraryName=rgss_runtime

# For arm64-v8a (devices)
./gradlew publishToMavenLocal -PtargetArch=arm64-v8a -PnativeLibraryName=rgss_runtime
```

## Why Not in Docker?

The Android Gradle Plugin requires the full Android SDK (platform APIs, build-tools, etc.), not just the NDK. Installing the full SDK in the Docker image would significantly increase its size and build time. Since Android development typically happens on the host machine where the SDK is already installed, it's more practical to run the publishing step on the host.

The Docker container is used for cross-compiling the native Ruby VM, which requires the NDK and specialized toolchains.
