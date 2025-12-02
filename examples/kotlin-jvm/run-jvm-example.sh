#!/bin/bash
# Script to compile and run the JVM Example

set -e  # Exit on error

echo "=== Running Ruby VM JVM Example ==="
echo ""

# Paths

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

cd "$PROJECT_ROOT"

echo "Project root: $PROJECT_ROOT"

JAR_PATH="kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"
EXAMPLE_FILE="$SCRIPT_DIR/JvmExample.kt"

if kotlinc -version &> /dev/null; then
    echo "✓ Kotlin compiler found"
else
    echo "Error: Kotlin compiler (kotlinc) not found. Please install it to proceed."
    exit 1
fi

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
kotlinc "$EXAMPLE_FILE" -cp "$JAR_PATH" -include-runtime -d "$SCRIPT_DIR/JvmExample.jar"
echo "✓ Compiled successfully"
echo ""

echo "Step 2: Running example..."
echo "----------------------------------------"
java -cp "$SCRIPT_DIR/JvmExample.jar:$JAR_PATH" examples.JvmExampleKt
echo "----------------------------------------"
echo ""
echo "✓ Example completed!"
