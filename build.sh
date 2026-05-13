#!/bin/bash

# QNode Build Script

set -e

BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-Release}

echo "QNode Build Script"
echo "=================="
echo "Build directory: $BUILD_DIR"
echo "Build type: $BUILD_TYPE"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..

# Build
echo "Building..."
cmake --build . --config "$BUILD_TYPE"

echo ""
echo "Build complete!"
echo "Executable: bin/qnode"
echo ""
echo "To test the build, run:"
echo "  ./bin/qnode ../test_simple.js"
