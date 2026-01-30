#!/bin/bash
# =============================================================================
# ICAO Local PKD - Build Script
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== ICAO Local PKD Build Script ===${NC}"
echo ""

# Check for vcpkg
if [ -z "$VCPKG_ROOT" ]; then
    echo -e "${YELLOW}Warning: VCPKG_ROOT not set${NC}"
    echo "Please set VCPKG_ROOT environment variable"
    echo "Example: export VCPKG_ROOT=/path/to/vcpkg"
    exit 1
fi

# Parse arguments
BUILD_TYPE="Release"
BUILD_TESTS="ON"
CLEAN_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo "Build Type: $BUILD_TYPE"
echo "Build Tests: $BUILD_TESTS"
echo ""

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$PROJECT_DIR/build"
fi

# Create build directory
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"

# Configure with CMake
echo -e "${GREEN}Configuring with CMake...${NC}"
cmake -S "$PROJECT_DIR" -B . \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBUILD_TESTS="$BUILD_TESTS"

# Build
echo ""
echo -e "${GREEN}Building...${NC}"
cmake --build . -j$(nproc)

echo ""
echo -e "${GREEN}Build completed successfully!${NC}"
echo "Binary: $PROJECT_DIR/build/bin/icao-local-pkd"

# Run tests if enabled
if [ "$BUILD_TESTS" = "ON" ]; then
    echo ""
    echo -e "${GREEN}Running tests...${NC}"
    ctest --output-on-failure
fi
