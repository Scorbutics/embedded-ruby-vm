#!/bin/bash
# Script to build and run the Kotlin/Native Ruby VM example

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../../.." && pwd )"

echo "=== Kotlin/Native Ruby VM Example ==="
echo
echo "Project root: $PROJECT_ROOT"
echo "Example dir: $SCRIPT_DIR"
echo

# Check prerequisites
if ! command -v ./gradlew &> /dev/null; then
    echo "Error: Gradle wrapper not found. Please use ./gradlew"
    exit 1
fi

# Step 1: Build native libraries if needed
echo "Step 1: Checking native libraries..."
NATIVE_LIB="$PROJECT_ROOT/kmp/libs/linux_x64/libruby-vm.a"

if [ ! -f "$NATIVE_LIB" ]; then
    echo "Native libraries not found. Building..."
    cd "$PROJECT_ROOT"
    rm -f CMakeCache.txt
    ./gradlew :ruby-vm-kmp:buildNativeLibsLinux -PtargetArch=x86_64
    echo "✓ Native libraries built"
else
    echo "✓ Native libraries found"
fi
echo

# Step 2: Ensure Ruby stdlib is extracted
echo "Step 2: Checking Ruby standard library..."
RUBY_DIR="$PROJECT_ROOT/ruby"

if [ ! -d "$RUBY_DIR" ]; then
    echo "Extracting Ruby standard library..."
    cd "$PROJECT_ROOT"
    unzip -q core/assets/files/ruby-stdlib.zip -d ruby
    echo "✓ Ruby stdlib extracted to $RUBY_DIR"
else
    echo "✓ Ruby stdlib found: $RUBY_DIR"
fi
echo

# Step 3: Build and run the example
echo "Step 3: Building and running Kotlin/Native example..."
cd "$SCRIPT_DIR"

# Use gradlew if available, otherwise gradle
if [ -f "./gradlew" ]; then
    GRADLE_CMD="./gradlew"
elif [ -f "$PROJECT_ROOT/gradlew" ]; then
    # Create a symlink to project root's gradlew
    ln -sf "$PROJECT_ROOT/gradlew" ./gradlew
    ln -sf "$PROJECT_ROOT/gradle" ./gradle
    GRADLE_CMD="./gradlew"
else
    GRADLE_CMD="gradle"
fi

$GRADLE_CMD runExample

echo
echo "================================================"
echo "✓ Example completed successfully!"
