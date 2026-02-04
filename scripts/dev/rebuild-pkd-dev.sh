#!/bin/bash

# ==============================================================================
# Rebuild PKD Management Development Service
# ==============================================================================
# Purpose: Rebuild and restart development service
# Options: --no-cache for clean build
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$PROJECT_ROOT/docker"

cd "$DOCKER_DIR"

# Parse arguments
NO_CACHE=""
if [ "$1" == "--no-cache" ]; then
    NO_CACHE="--no-cache"
    echo "=== Clean build requested (--no-cache) ==="
else
    echo "=== Incremental build (use --no-cache for clean build) ==="
fi

echo ""
echo "Stopping development service..."
docker-compose -f docker-compose.dev.yaml down

echo ""
echo "Building pkd-management-dev..."
docker-compose -f docker-compose.dev.yaml build $NO_CACHE pkd-management-dev

echo ""
echo "Starting pkd-management-dev..."
docker-compose -f docker-compose.dev.yaml up -d pkd-management-dev

echo ""
echo "=== Development Service Rebuilt ==="
echo "Container: icao-pkd-management-dev"
echo "Port: 8091"
echo ""
echo "Check logs: ./scripts/dev/logs-pkd-dev.sh"
echo "Check health: curl http://localhost:8091/api/health"
echo ""
