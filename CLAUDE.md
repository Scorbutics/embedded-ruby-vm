# Embedded Ruby VM - Development Guide

## Interaction Rules

- **Challenge architectural decisions.** Push back on choices that seem wrong or suboptimal. The goal is to avoid blind spots — don't just agree.
- **Ask, don't guess.** When something is ambiguous, ask a question rather than inferring from context. Only proceed without asking for straightforward, low-risk decisions.
- **Follow this file.** Always respect the rules and constraints defined in this CLAUDE.md.

## Critical: Docker Development Environment

All build/test commands MUST run inside the docker container (`embedded-ruby-vm-dev`).
Prefix commands with `docker exec embedded-ruby-vm-dev` or use `docker-dev.sh`.

The docker-compose stack uses **named volumes** (not bind mounts). After source changes,
resync with: `docker-compose run --rm source-sync-in`

See [README.docker.md](README.docker.md) for full Docker setup.

## Build & Test

```bash
./gradlew build                              # Build all (auto-detects OS/arch)
./gradlew build -PtargetArch=x86_64          # Specific arch
./gradlew build -PbuildType=Debug            # Debug build

# Tests (after build)
cd build
./bin/test_core                              # Core C library tests
./bin/test_jni                               # JNI layer tests
./bin/test_jni_android_log                   # Android logging tests

# Examples
cd examples/kotlin-jvm && ../../gradlew runExample
```

## Architecture

Three-layer design: C Core -> Low-Level Kotlin -> High-Level Kotlin.
See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design decisions, library loading,
and cross-platform patterns.

## Code Style

**Kotlin:**
- Use `actual`/`expect` for multi-platform APIs
- Prefer extension functions over static methods
- Use sealed classes for type-safe error handling

**C:**
- Naming: `ruby_vm_*`, `ruby_script_*`
- Doxygen-style comments
- Keep platform-specific code isolated
- Error handling via `RubyVMError`

## Project Layout

```
core/           C library (ruby-vm/, assets/, logging/, external/)
jni/            JNI bindings for Android/JVM
kmp/            Kotlin Multiplatform module
  src/commonMain/   Shared API definitions
  src/androidMain/  Android (JNI)
  src/desktopMain/  JVM Desktop (JNI)
  src/nativeMain/   Linux/iOS/macOS (cinterop)
  src/desktopTest/  KMP unit tests
tests/          Native C tests (core/, jni/, jni-android/)
examples/       Usage examples (java/, kotlin-jvm/, kotlin-native/)
```

## Platform Builds

```bash
# Android
./gradlew :ruby-vm-kmp:assembleDebug -PtargetArch=arm64
./gradlew :ruby-vm-kmp:assembleRelease -PtargetArch=all

# Desktop JVM
./gradlew :ruby-vm-kmp:desktopJar

# Linux Native (no JVM)
./gradlew :ruby-vm-kmp:linuxX64MainBinaries

# All native libs
./gradlew buildAllNativeLibs
```
