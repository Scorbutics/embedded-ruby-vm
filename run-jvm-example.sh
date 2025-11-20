#!/bin/bash
# Script to compile and run the JVM Example

set -e  # Exit on error

echo "=== Running Ruby VM JVM Example ==="
echo ""

# Paths
JAR_PATH="kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"
LIB_PATH="build/jvm/lib"
EXAMPLE_FILE="examples/JvmExample.kt"

# Check if JAR exists
if [ ! -f "$JAR_PATH" ]; then
    echo "Error: JAR not found at $JAR_PATH"
    echo "Run: ./gradlew :ruby-vm-kmp:desktopJar"
    exit 1
fi

# Check if native library exists
if [ ! -f "$LIB_PATH/libembedded-ruby.so" ]; then
    echo "Error: Native library not found at $LIB_PATH/libembedded-ruby.so"
    echo "Run: cmake --build build/jvm"
    exit 1
fi

# Check if example file exists
if [ ! -f "$EXAMPLE_FILE" ]; then
    echo "Error: Example file not found at $EXAMPLE_FILE"
    exit 1
fi

echo "Step 1: Compiling Kotlin example..."
kotlinc "$EXAMPLE_FILE" -cp "$JAR_PATH" -include-runtime -d examples/JvmExample.jar
echo "✓ Compiled successfully"
echo ""

echo "Step 2: Running example..."
echo "----------------------------------------"
java -Djava.library.path="$LIB_PATH" \
     -cp "examples/JvmExample.jar:$JAR_PATH" \
     examples.JvmExampleKt
echo "----------------------------------------"
echo ""
echo "✓ Example completed!"
