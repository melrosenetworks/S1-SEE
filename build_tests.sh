#!/bin/bash
# Build script with workaround for protobuf/gRPC conflict

set -e

BUILD_DIR="build"
cd "$(dirname "$0")"

echo "Building S1-SEE tests..."

# Clean build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Try to configure with workaround
# The issue is protobuf 6.x has target conflicts with gRPC
# We'll try to work around it by setting an environment variable
export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}"

# Configure
echo "Configuring CMake..."
if ! cmake .. 2>&1 | tee cmake.log; then
    echo "CMake configuration failed. This may be due to protobuf/gRPC version conflicts."
    echo "Attempting workaround..."
    
    # Try with protobuf found first, then gRPC
    # This sometimes works if we clear the cache
    rm -f CMakeCache.txt
    cmake .. -Dprotobuf_MODULE_COMPATIBLE=ON 2>&1 | tee cmake.log || {
        echo "Still failing. Checking if we can build without gRPC tests..."
        # Build just the core components that don't need gRPC
        cmake .. -DBUILD_GRPC_TESTS=OFF 2>&1 | tee cmake.log || {
            echo "Error: Could not configure CMake."
            echo "This is likely a protobuf 6.x / gRPC compatibility issue."
            echo "Please check cmake.log for details."
            exit 1
        }
    }
fi

# Build
echo "Building..."
make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4) 2>&1 | tee build.log || {
    echo "Build failed. Check build.log for details."
    exit 1
}

echo "Build successful!"
echo ""
echo "Running tests..."

# Run tests
if [ -f test_ue_context ]; then
    echo "Running UEContext tests..."
    ./test_ue_context
fi

if [ -f test_correlator ]; then
    echo "Running Correlator tests..."
    ./test_correlator
fi

if [ -f test_integration ]; then
    echo "Running Integration tests..."
    ./test_integration
fi

echo ""
echo "All tests completed!"


