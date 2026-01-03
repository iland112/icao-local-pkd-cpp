#!/bin/bash
# =============================================================================
# ARM64 Docker Image Build Script
# Uses Docker Buildx for cross-compilation
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# Configuration
REGISTRY=${REGISTRY:-""}
TAG=${TAG:-"arm64"}
PUSH=${PUSH:-"false"}
PLATFORM="linux/arm64"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}🚀 ARM64 Docker Image Build${NC}"
echo "Platform: $PLATFORM"
echo "Tag: $TAG"
echo ""

# Ensure buildx builder exists
echo -e "${YELLOW}📦 Checking Docker Buildx...${NC}"
if ! docker buildx inspect arm64-builder > /dev/null 2>&1; then
    echo "Creating arm64-builder..."
    docker buildx create --name arm64-builder --driver docker-container --platform linux/amd64,linux/arm64 --use
fi
docker buildx use arm64-builder
docker buildx inspect --bootstrap > /dev/null 2>&1

echo -e "${GREEN}✓ Buildx ready${NC}"
echo ""

# Build options
BUILD_OPTS="--platform $PLATFORM"
if [ "$PUSH" = "true" ] && [ -n "$REGISTRY" ]; then
    BUILD_OPTS="$BUILD_OPTS --push"
else
    BUILD_OPTS="$BUILD_OPTS --load"
fi

# Build PKD Management Service
echo -e "${YELLOW}🔨 Building PKD Management Service...${NC}"
docker buildx build $BUILD_OPTS \
    -t ${REGISTRY}icao-local-pkd-management:$TAG \
    -f services/pkd-management/Dockerfile \
    .
echo -e "${GREEN}✓ PKD Management built${NC}"
echo ""

# Build PA Service
echo -e "${YELLOW}🔨 Building PA Service...${NC}"
docker buildx build $BUILD_OPTS \
    -t ${REGISTRY}icao-local-pkd-pa:$TAG \
    -f services/pa-service/Dockerfile \
    .
echo -e "${GREEN}✓ PA Service built${NC}"
echo ""

# Build Sync Service
echo -e "${YELLOW}🔨 Building Sync Service...${NC}"
docker buildx build $BUILD_OPTS \
    -t ${REGISTRY}icao-local-pkd-sync:$TAG \
    -f services/sync-service/Dockerfile \
    .
echo -e "${GREEN}✓ Sync Service built${NC}"
echo ""

# Build Frontend
echo -e "${YELLOW}🔨 Building Frontend...${NC}"
docker buildx build $BUILD_OPTS \
    -t ${REGISTRY}icao-local-pkd-frontend:$TAG \
    -f frontend/Dockerfile \
    frontend/
echo -e "${GREEN}✓ Frontend built${NC}"
echo ""

# Summary
echo -e "${GREEN}✅ All ARM64 images built successfully!${NC}"
echo ""
echo "Images created:"
echo "  - icao-local-pkd-management:$TAG"
echo "  - icao-local-pkd-pa:$TAG"
echo "  - icao-local-pkd-sync:$TAG"
echo "  - icao-local-pkd-frontend:$TAG"
echo ""

if [ "$PUSH" != "true" ]; then
    echo "To export images for deployment:"
    echo "  docker save icao-local-pkd-management:$TAG | gzip > pkd-management-arm64.tar.gz"
    echo "  docker save icao-local-pkd-pa:$TAG | gzip > pkd-pa-arm64.tar.gz"
    echo "  docker save icao-local-pkd-sync:$TAG | gzip > pkd-sync-arm64.tar.gz"
    echo "  docker save icao-local-pkd-frontend:$TAG | gzip > pkd-frontend-arm64.tar.gz"
    echo ""
    echo "To transfer to target:"
    echo "  scp *.tar.gz luckfox@192.168.100.11:~/"
fi
