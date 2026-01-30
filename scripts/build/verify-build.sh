#!/bin/bash
# Build Verification Script
# Usage: ./scripts/verify-build.sh pkd-management v2.0.7 "parseMasterListEntryV2"

set -e

SERVICE=$1
EXPECTED_VERSION=$2
EXPECTED_FUNCTION=$3

if [ -z "$SERVICE" ] || [ -z "$EXPECTED_VERSION" ]; then
    echo "Usage: $0 <service> <expected-version> [expected-function]"
    echo "Example: $0 pkd-management v2.0.7 parseMasterListEntryV2"
    exit 1
fi

CONTAINER_NAME="icao-local-pkd-${SERVICE}"
IMAGE_NAME="docker-${SERVICE}:latest"

echo "=================================="
echo "Build Verification for ${SERVICE}"
echo "=================================="
echo ""

# 1. Check if container is running
echo "1. Container Status:"
if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "   ✅ Container is running"
else
    echo "   ❌ Container is not running"
    exit 1
fi

# 2. Check image creation time
echo ""
echo "2. Image Build Time:"
IMAGE_TIME=$(docker images --format "{{.CreatedAt}}" "$IMAGE_NAME" 2>/dev/null | head -1)
if [ -z "$IMAGE_TIME" ]; then
    echo "   ❌ Image not found"
    exit 1
fi
echo "   Created: $IMAGE_TIME"

# Calculate age in minutes
IMAGE_EPOCH=$(docker images --format "{{.CreatedAt}}" "$IMAGE_NAME" | head -1 | xargs -I {} date -d {} +%s 2>/dev/null || echo 0)
NOW_EPOCH=$(date +%s)
AGE_MINUTES=$(( (NOW_EPOCH - IMAGE_EPOCH) / 60 ))

if [ $AGE_MINUTES -lt 10 ]; then
    echo "   ✅ Image is fresh (${AGE_MINUTES} minutes old)"
else
    echo "   ⚠️  Image is ${AGE_MINUTES} minutes old (expected < 10 minutes)"
    echo "   Did you forget to rebuild with --no-cache?"
fi

# 3. Check version string in logs
echo ""
echo "3. Version String in Logs:"
ACTUAL_VERSION=$(docker logs "$CONTAINER_NAME" 2>&1 | grep "======" | tail -1)
if echo "$ACTUAL_VERSION" | grep -q "$EXPECTED_VERSION"; then
    echo "   ✅ Found: $ACTUAL_VERSION"
else
    echo "   ❌ Expected version not found in logs"
    echo "   Looking for: $EXPECTED_VERSION"
    echo "   Found: $ACTUAL_VERSION"
    exit 1
fi

# 4. Check binary for expected function (if provided)
if [ -n "$EXPECTED_FUNCTION" ]; then
    echo ""
    echo "4. Binary Function Check:"
    if docker exec "$CONTAINER_NAME" strings "/app/${SERVICE}" 2>/dev/null | grep -q "$EXPECTED_FUNCTION"; then
        echo "   ✅ Function '$EXPECTED_FUNCTION' found in binary"
    else
        echo "   ❌ Function '$EXPECTED_FUNCTION' NOT found in binary"
        echo "   This indicates the code was not compiled!"
        exit 1
    fi
fi

# 5. Container health
echo ""
echo "5. Container Health:"
HEALTH=$(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo "unknown")
if [ "$HEALTH" == "healthy" ]; then
    echo "   ✅ Container is healthy"
elif [ "$HEALTH" == "unknown" ]; then
    echo "   ⚠️  No healthcheck defined"
else
    echo "   ❌ Container health: $HEALTH"
fi

echo ""
echo "=================================="
echo "✅ All checks passed!"
echo "=================================="
