#!/bin/bash
# Frontend Build Verification Script
# Purpose: Verify that the running frontend container has the latest build
# Usage: ./scripts/verify-frontend-build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPOSE_FILE="$PROJECT_ROOT/docker/docker-compose.yaml"

echo "=================================="
echo "Frontend Build Verification"
echo "=================================="
echo ""

# Check if frontend container is running
if ! docker compose -f "$COMPOSE_FILE" ps frontend | grep -q "Up"; then
    echo "❌ Frontend container is not running"
    exit 1
fi

# Get local build hash
LOCAL_BUILD_DIR="$PROJECT_ROOT/frontend/dist"
if [ ! -d "$LOCAL_BUILD_DIR" ]; then
    echo "⚠️  No local build found at $LOCAL_BUILD_DIR"
    echo "   Run 'npm run build' first"
    exit 1
fi

LOCAL_JS_FILE=$(ls -t "$LOCAL_BUILD_DIR/assets/index-"*.js 2>/dev/null | head -1 | xargs basename)
LOCAL_BUILD_TIME=$(stat -c %Y "$LOCAL_BUILD_DIR/assets/$LOCAL_JS_FILE" 2>/dev/null || stat -f %m "$LOCAL_BUILD_DIR/assets/$LOCAL_JS_FILE" 2>/dev/null)
LOCAL_FILE_SIZE=$(stat -c %s "$LOCAL_BUILD_DIR/assets/$LOCAL_JS_FILE" 2>/dev/null || stat -f %z "$LOCAL_BUILD_DIR/assets/$LOCAL_JS_FILE" 2>/dev/null)

echo "Local build:"
echo "  File: $LOCAL_JS_FILE"
echo "  Size: $LOCAL_FILE_SIZE bytes"
echo "  Time: $(date -d @$LOCAL_BUILD_TIME 2>/dev/null || date -r $LOCAL_BUILD_TIME 2>/dev/null)"
echo ""

# Get container build
CONTAINER_JS_FILE=$(docker compose -f "$COMPOSE_FILE" exec -T frontend ls -1 /usr/share/nginx/html/assets/ | grep -o 'index-[A-Z0-9a-z]*\.js' | head -1)
CONTAINER_FILE_SIZE=$(docker compose -f "$COMPOSE_FILE" exec -T frontend stat -c %s "/usr/share/nginx/html/assets/$CONTAINER_JS_FILE" 2>/dev/null || \
                      docker compose -f "$COMPOSE_FILE" exec -T frontend stat -f %z "/usr/share/nginx/html/assets/$CONTAINER_JS_FILE" 2>/dev/null)

echo "Container build:"
echo "  File: $CONTAINER_JS_FILE"
echo "  Size: $CONTAINER_FILE_SIZE bytes"
echo ""

# Compare
echo "Comparison:"
if [ "$LOCAL_JS_FILE" = "$CONTAINER_JS_FILE" ]; then
    if [ "$LOCAL_FILE_SIZE" = "$CONTAINER_FILE_SIZE" ]; then
        echo "✅ MATCH: Container is using the latest build"
        echo ""

        # Get container image hash
        CONTAINER_IMAGE=$(docker compose -f "$COMPOSE_FILE" ps frontend --format json | jq -r '.[0].Image' 2>/dev/null || \
                         docker compose -f "$COMPOSE_FILE" ps frontend | grep frontend | awk '{print $2}')
        echo "Container image: $CONTAINER_IMAGE"

        # Get image creation time
        IMAGE_CREATED=$(docker inspect "$CONTAINER_IMAGE" --format='{{.Created}}' 2>/dev/null)
        if [ -n "$IMAGE_CREATED" ]; then
            echo "Image created: $IMAGE_CREATED"
        fi

        exit 0
    else
        echo "⚠️  MISMATCH: Same filename but different size"
        echo "   Local:     $LOCAL_FILE_SIZE bytes"
        echo "   Container: $CONTAINER_FILE_SIZE bytes"
        echo ""
        echo "This might indicate a build cache issue."
        echo "Recommendation: Run ./scripts/frontend-rebuild.sh"
        exit 1
    fi
else
    echo "❌ MISMATCH: Container is using an old build"
    echo "   Local:     $LOCAL_JS_FILE"
    echo "   Container: $CONTAINER_JS_FILE"
    echo ""
    echo "Recommendation: Run ./scripts/frontend-rebuild.sh"
    exit 1
fi
