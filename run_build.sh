#!/bin/bash
# Incremental build script for Trace/KiCad
#
# Three separate backend servers:
#   --debug      Local dev server      (localhost:8000)
#   --staging    Railway server        (trace-staging.up.railway.app)
#   (default)    EC2 production server (api.buildwithtrace.com)
#
# Other options:
#   --full       Force full reconfigure with cmake
#   --no-install Skip the install step (faster for testing compilation)

cd build/release

FULL_BUILD=false
SKIP_INSTALL=false
BUILD_TYPE="Release"
BACKEND_URL="https://api.buildwithtrace.com/api/v1"

for arg in "$@"; do
    case $arg in
        --full)
            FULL_BUILD=true
            ;;
        --no-install)
            SKIP_INSTALL=true
            ;;
        --staging)
            BUILD_TYPE="Staging"
            BACKEND_URL="https://trace-staging.up.railway.app/api/v1"
            ;;
        --debug)
            BUILD_TYPE="Debug"
            BACKEND_URL="http://localhost:8000/api/v1"
            ;;
    esac
done

echo "Build configuration:"
echo "  BUILD_TYPE: $BUILD_TYPE"
echo "  BACKEND_URL: $BACKEND_URL"

# Only run cmake if CMakeCache.txt doesn't exist or --full flag is passed
if [ ! -f "CMakeCache.txt" ] || [ "$FULL_BUILD" = true ]; then
    echo "Running CMake configuration..."
    
    CMAKE_ARGS=(
        -DTRACE_BACKEND_URL="$BACKEND_URL"
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_TOOLCHAIN_FILE=../trace-mac-builder/toolchain/kicad-mac-builder.cmake
        ../..
    )
    
    cmake -G Ninja "${CMAKE_ARGS[@]}"
else
    echo "Skipping CMake (already configured). Use --full to force reconfigure."
fi

# Get number of CPU cores for parallel build
CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "Building with $CORES parallel jobs..."

ninja -j$CORES

if [ "$SKIP_INSTALL" = false ]; then
    echo "Installing..."
    ninja install
else
    echo "Skipping install step (use without --no-install to install)"
fi

echo "Build complete!"
