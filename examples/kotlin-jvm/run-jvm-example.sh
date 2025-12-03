#!/bin/bash
# Script to compile and run the JVM Example

set -e  # Exit on error

echo "=== Running Ruby VM JVM Example ==="
echo ""

# Paths

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

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

# Run the example using Gradle
# This will:
# 1. Build the KMP desktop JAR if needed
# 2. Compile the Java example
# 3. Run the example with proper classpath and java.library.path
$GRADLE_CMD runExample -PbuildType=Debug --no-build-cache

echo
echo "✓ Example completed successfully!"
