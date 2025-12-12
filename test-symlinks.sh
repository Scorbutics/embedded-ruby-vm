#!/bin/bash
# Test script to verify symlink creation for native libraries

echo "=== Testing Library Symlink Creation ==="
echo ""

# Run test inside Docker
docker exec -it embedded-ruby-vm-dev bash -c '
cd /workspace/kmp/build/cmake/desktop-linux-x86_64 || exit 1

# Run the test
echo "Running test_core..."
./bin/test_core

# Check if test-ruby-install was created
if [ -d "./test-ruby-install/native-libs" ]; then
    echo ""
    echo "=== Checking created symlinks ==="
    find ./test-ruby-install/native-libs -type l -ls
    echo ""
    echo "=== Native libraries directory contents ==="
    ls -lah ./test-ruby-install/native-libs/*/
else
    echo "ERROR: test-ruby-install directory not created"
    exit 1
fi
'
