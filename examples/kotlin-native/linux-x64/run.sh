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

rm -f $PROJECT_ROOT/CMakeCache.txt

echo "Building and running Kotlin/Native example..."
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

$GRADLE_CMD runExample --no-build-cache

echo
echo "================================================"
echo "✓ Example completed successfully!"
