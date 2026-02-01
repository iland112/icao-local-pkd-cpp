#!/bin/bash

# =============================================================================
# Rebuild PA Service Development Container
# =============================================================================
# Purpose: Rebuild and restart pa-service-dev after code changes
# Usage: ./scripts/dev/rebuild-pa-dev.sh [--no-cache]
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

NO_CACHE=""
if [[ "$1" == "--no-cache" ]]; then
    NO_CACHE="--no-cache"
    echo "Building with --no-cache flag"
fi

echo "=========================================="
echo "Rebuilding PA Service Development Container"
echo "=========================================="
echo ""

# Stop current container
echo "Stopping pa-service-dev..."
docker-compose -f docker-compose.dev.yml stop pa-service-dev

# Rebuild
echo ""
echo "Building pa-service-dev $NO_CACHE..."
docker-compose -f docker-compose.dev.yml build $NO_CACHE pa-service-dev

# Restart with force-recreate
echo ""
echo "Starting pa-service-dev..."
docker-compose -f docker-compose.dev.yml up -d --force-recreate pa-service-dev

echo ""
echo "Waiting for service to be healthy..."
sleep 5

# Health check
if curl -s http://localhost:8092/api/health > /dev/null 2>&1; then
    echo "✅ PA Service Dev rebuilt and running!"
    echo ""
    echo "View logs:"
    echo "  docker-compose -f docker-compose.dev.yml logs -f pa-service-dev"
else
    echo "⚠️  Service started but health check failed"
    echo "   Check logs: docker-compose -f docker-compose.dev.yml logs pa-service-dev"
    exit 1
fi

echo ""
echo "Done!"
