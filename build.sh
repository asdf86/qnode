#!/bin/bash

# QNode Build Script for MSYS2 ucrt2 Environment

set -e

# Set MSYS2 ucrt2 environment
export MSYS2_UCRT_PATH="/c/msys64/ucrt64"
export CC="/c/msys64/ucrt64/bin/gcc.exe"
export CXX="/c/msys64/ucrt64/bin/g++.exe"
export PATH="/c/msys64/ucrt64/bin:$PATH"

BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-Release}

echo "=== QNode Build Script (MSYS2 ucrt2) ==="
echo "Compiler: $CC"
echo "Build directory: $BUILD_DIR"
echo "Build type: $BUILD_TYPE"
echo ""

# Clean previous build
echo "Cleaning previous build..."
rm -rf "$BUILD_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake -B "$BUILD_DIR" -S . -G Ninja \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "Building..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

echo ""
echo "=== Build Complete! ==="
echo "Executable: $BUILD_DIR/bin/qnode"
echo ""

# Run tests
echo "=== Running Tests ==="
echo ""

if [ -f "test_no_import.js" ]; then
    echo "Test 1: Basic functionality"
    "$BUILD_DIR/bin/qnode" test_no_import.js || echo "Test 1 failed"
fi

if [ -f "test_very_simple.js" ]; then
    echo ""
    echo "Test 2: Process module"
    "$BUILD_DIR/bin/qnode" test_very_simple.js || echo "Test 2 failed"
fi

if [ -f "test/test_process.js" ]; then
    echo ""
    echo "Test 3: Full process test"
    "$BUILD_DIR/bin/qnode" test/test_process.js || echo "Test 3 failed"
fi

echo ""
echo "=== All Tests Complete ==="
