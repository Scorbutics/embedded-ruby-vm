#!/bin/sh
# Script to compile and run the JVM Example

set -e  # Exit on error

echo "=== Running Ruby VM JVM Example ==="
echo ""

# Paths
JAR_PATH="kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"
EXAMPLE_FILE="examples/JvmExample.kt"

# Check if JAR exists
if [ ! -f "$JAR_PATH" ]; then
    echo "Error: JAR not found at $JAR_PATH"
    echo "Run: ./gradlew :ruby-vm-kmp:desktopJar"
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
java -cp "examples/JvmExample.jar:$JAR_PATH" examples.JvmExampleKt
echo "----------------------------------------"
echo ""
echo "✓ Example completed!"
