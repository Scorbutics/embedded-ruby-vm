#!/bin/bash
# Script to build and run SimpleJavaExample.java

set -e

echo "=== Building and Running SimpleJavaExample.java ==="
echo

# Get the project root directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

cd "$PROJECT_ROOT"

echo "Project root: $PROJECT_ROOT"

# Step 2: Build the KMP JAR if needed
echo "Step 2: Checking KMP JAR..."
if [ ! -f "kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar" ]; then
    echo "KMP JAR not found. Building..."
    ./gradlew :ruby-vm-kmp:desktopJar
else
    echo "✓ KMP JAR found: kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"
fi
echo

# Step 3: Ensure Ruby stdlib is extracted
echo "Step 3: Checking Ruby standard library..."
if [ ! -d "ruby" ]; then
    echo "Extracting Ruby standard library..."
    unzip -q core/assets/files/ruby-stdlib.zip -d ruby
    echo "✓ Ruby stdlib extracted to ./ruby/"
else
    echo "✓ Ruby stdlib found: ./ruby/"
fi
echo

# Step 4: Create lib directory and copy native library
echo "Step 4: Setting up lib directory..."
mkdir -p lib
cp build/jvm/lib/libembedded-ruby.so lib/
echo "✓ Native library copied to ./lib/"
echo

# Step 5: Compile Java example
echo "Step 5: Compiling SimpleJavaExample.java..."
javac -cp kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
    "$SCRIPT_DIR/SimpleJavaExample.java"
echo "✓ Compiled successfully!"
echo

# Step 6: Run the example
echo "Step 6: Running SimpleJavaExample..."
echo "================================================"
echo
java -cp kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar:examples \
    -Djava.library.path=lib \
    examples.SimpleJavaExample

echo
echo "================================================"
echo "✓ Example completed successfully!"
echo
echo "Cleanup: Removing compiled .class file..."
rm -f "$SCRIPT_DIR/SimpleJavaExample.class"
echo "✓ Done!"
