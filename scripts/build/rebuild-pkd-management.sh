#!/bin/bash
# PKD Management Service 재빌드 및 배포 스크립트
# Usage: ./scripts/build/rebuild-pkd-management.sh [--no-cache]

set -e

VERSION="v2.3.0"
IMAGE_NAME="icao-pkd-management:${VERSION}"
DOCKERFILE="services/pkd-management/Dockerfile"

echo "=========================================="
echo "PKD Management Service Rebuild"
echo "Version: ${VERSION}"
echo "=========================================="

# Parse arguments
NO_CACHE=""
if [[ "$1" == "--no-cache" ]]; then
    NO_CACHE="--no-cache"
    echo "⚠️  No-cache build requested (slower but guaranteed fresh)"
fi

# Step 1: Stop and remove current container
echo ""
echo "Step 1: Stopping current container..."
docker-compose -f docker/docker-compose.yaml stop pkd-management || true
docker-compose -f docker/docker-compose.yaml rm -f pkd-management || true

# Step 2: Build new image
echo ""
echo "Step 2: Building new image ${IMAGE_NAME}..."
docker build ${NO_CACHE} -t ${IMAGE_NAME} -f ${DOCKERFILE} .

# Step 3: Verify build
echo ""
echo "Step 3: Verifying build..."
docker run --rm ${IMAGE_NAME} /app/pkd-management --version 2>&1 | head -5 || echo "Version check not available"

# Step 4: Start new container
echo ""
echo "Step 4: Starting new container..."
docker-compose -f docker/docker-compose.yaml up -d pkd-management

# Step 5: Wait and check logs
echo ""
echo "Step 5: Checking startup logs..."
sleep 5
docker logs icao-local-pkd-management --tail 20

echo ""
echo "=========================================="
echo "✅ Rebuild complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  - Check health: docker logs icao-local-pkd-management --tail 50"
echo "  - Test API: curl http://localhost:8080/api/health"
echo "  - Test upload: curl http://localhost:8080/api/upload/history"
echo "  - Test countries: curl http://localhost:8080/api/upload/countries/detailed?limit=10"
