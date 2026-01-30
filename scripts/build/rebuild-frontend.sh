#!/bin/bash
# Frontend Rebuild and Redeploy Script
# Purpose: Ensure frontend is built with latest code and deployed correctly
# Usage: ./scripts/frontend-rebuild.sh

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FRONTEND_DIR="$PROJECT_ROOT/frontend"
COMPOSE_FILE="$PROJECT_ROOT/docker/docker-compose.yaml"

echo "=================================="
echo "Frontend Rebuild and Redeploy"
echo "=================================="
echo ""

# Step 1: Build frontend locally
echo "[1/5] Building frontend locally..."
cd "$FRONTEND_DIR"
npm run build

if [ $? -ne 0 ]; then
    echo "❌ Frontend build failed"
    exit 1
fi

# Get the new build hash from generated files
NEW_JS_FILE=$(ls -t dist/assets/index-*.js 2>/dev/null | head -1 | xargs basename)
echo "✅ Frontend built successfully: $NEW_JS_FILE"
echo ""

# Step 2: Stop and remove old frontend container
echo "[2/5] Stopping and removing old frontend container..."
cd "$PROJECT_ROOT"
docker compose -f "$COMPOSE_FILE" rm -sf frontend
echo "✅ Old container removed"
echo ""

# Step 3: Remove old frontend image to force rebuild
echo "[3/5] Removing old frontend image..."
OLD_IMAGE=$(docker images docker-frontend:latest -q)
if [ -n "$OLD_IMAGE" ]; then
    docker rmi -f docker-frontend:latest 2>/dev/null || true
    echo "✅ Old image removed: $OLD_IMAGE"
else
    echo "ℹ️  No old image found"
fi
echo ""

# Step 4: Build new frontend image (without building other services)
echo "[4/5] Building new frontend Docker image..."
docker build -t docker-frontend:latest \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    -f "$FRONTEND_DIR/Dockerfile" \
    "$FRONTEND_DIR"

if [ $? -ne 0 ]; then
    echo "❌ Docker build failed"
    exit 1
fi

NEW_IMAGE=$(docker images docker-frontend:latest -q)
echo "✅ New image built: $NEW_IMAGE"
echo ""

# Step 5: Start new frontend container
echo "[5/5] Starting new frontend container..."
docker compose -f "$COMPOSE_FILE" up -d frontend

if [ $? -ne 0 ]; then
    echo "❌ Failed to start frontend container"
    exit 1
fi

# Wait for container to be ready
echo "Waiting for container to be ready..."
sleep 3

# Verify new build is in container
echo ""
echo "Verifying new build in container..."
CONTAINER_JS_FILE=$(docker compose -f "$COMPOSE_FILE" exec -T frontend ls /usr/share/nginx/html/assets/ | grep -o 'index-[A-Z0-9a-z]*\.js' | head -1)

if [ "$CONTAINER_JS_FILE" = "$NEW_JS_FILE" ]; then
    echo "✅ Verification successful: $CONTAINER_JS_FILE"
else
    echo "⚠️  Warning: Container has $CONTAINER_JS_FILE but expected $NEW_JS_FILE"
    echo "   This might be normal if filenames match but content differs"
fi

echo ""
echo "=================================="
echo "Frontend rebuild complete!"
echo "=================================="
echo ""
echo "Next steps:"
echo "1. Open browser and press Ctrl+Shift+R to force refresh"
echo "2. Check browser console for any errors"
echo ""
echo "Container info:"
docker compose -f "$COMPOSE_FILE" ps frontend
